//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/benchmark.h"
#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/MpscPollableQueue.h"
#include "td/utils/port/sleep.h"
#include "td/utils/port/thread.h"
#include "td/utils/queue.h"
#include "td/utils/Random.h"

// TODO: check system calls
// TODO: all return values must be checked

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <sys/syscall.h>
#include <unistd.h>

#if TD_LINUX
#include <sys/eventfd.h>
#endif

#define MODE std::memory_order_relaxed

// void set_affinity(int mask) {
// int err, syscallres;
// pid_t pid = gettid();
// syscallres = syscall(__NR_sched_setaffinity, pid, sizeof(mask), &mask);
// if (syscallres) {
// err = errno;
// perror("oppa");
//}
//}

// TODO: warnings and asserts. There should be no warnings or debug output in production.
using qvalue_t = int;

// Just for testing, not production
class PipeQueue {
  int input;
  int output;

 public:
  void init() {
    int new_pipe[2];
    int res = pipe(new_pipe);
    CHECK(res == 0);
    output = new_pipe[0];
    input = new_pipe[1];
  }

  void put(qvalue_t value) {
    auto len = write(input, &value, sizeof(value));
    CHECK(len == sizeof(value));
  }

  qvalue_t get() {
    qvalue_t res;
    auto len = read(output, &res, sizeof(res));
    CHECK(len == sizeof(res));
    return res;
  }

  void destroy() {
    close(input);
    close(output);
  }
};

class Backoff {
  int cnt;

 public:
  Backoff() : cnt(0) {
  }

  bool next() {
    cnt++;
    if (cnt < 50) {
      return true;
    } else {
      sched_yield();
      return cnt < 500;
    }
  }
};

class VarQueue {
  std::atomic<qvalue_t> data{0};

 public:
  void init() {
    data.store(-1, MODE);
  }

  void put(qvalue_t value) {
    data.store(value, MODE);
  }

  qvalue_t try_get() {
    __sync_synchronize();  // TODO: it is wrong place for barrier, but it results in fastest queue
    qvalue_t res = data.load(MODE);
    return res;
  }

  void acquire() {
    data.store(-1, MODE);
  }

  qvalue_t get() {
    qvalue_t res;
    Backoff backoff;

    do {
      res = try_get();
    } while (res == -1 && (backoff.next(), true));
    acquire();

    return res;
  }

  void destroy() {
  }
};

class SemQueue {
  sem_t sem;
  VarQueue q;

 public:
  void init() {
    q.init();
    sem_init(&sem, 0, 0);
  }

  void put(qvalue_t value) {
    q.put(value);
    sem_post(&sem);
  }

  qvalue_t get() {
    sem_wait(&sem);
    qvalue_t res = q.get();
    return res;
  }

  void destroy() {
    q.destroy();
    sem_destroy(&sem);
  }

  // HACK for benchmark
  void reader_flush() {
  }

  void writer_flush() {
  }

  void writer_put(qvalue_t value) {
    put(value);
  }

  int reader_wait() {
    return 1;
  }

  qvalue_t reader_get_unsafe() {
    return get();
  }
};

#if TD_LINUX
class EventfdQueue {
  int fd;
  VarQueue q;

 public:
  void init() {
    q.init();
    fd = eventfd(0, 0);
  }
  void put(qvalue_t value) {
    q.put(value);
    td::int64 x = 1;
    auto len = write(fd, &x, sizeof(x));
    CHECK(len == sizeof(x));
  }
  qvalue_t get() {
    td::int64 x;
    auto len = read(fd, &x, sizeof(x));
    CHECK(len == sizeof(x));
    CHECK(x == 1);
    return q.get();
  }
  void destroy() {
    q.destroy();
    close(fd);
  }
};
#endif

const int queue_buf_size = 1 << 10;

class BufferQueue {
  struct node {
    qvalue_t val;
    char pad[64 - sizeof(std::atomic<qvalue_t>)];
  };
  node q[queue_buf_size];

  struct Position {
    std::atomic<td::uint32> i{0};
    char pad[64 - sizeof(std::atomic<td::uint32>)];

    td::uint32 local_read_i;
    td::uint32 local_write_i;
    char pad2[64 - sizeof(td::uint32) * 2];

    void init() {
      i = 0;
      local_read_i = 0;
      local_write_i = 0;
    }
  };

  Position writer;
  Position reader;

 public:
  void init() {
    writer.init();
    reader.init();
  }

  bool reader_empty() {
    return reader.local_write_i == reader.local_read_i;
  }

  bool writer_empty() {
    return writer.local_write_i == writer.local_read_i + queue_buf_size;
  }

  int reader_ready() {
    return static_cast<int>(reader.local_write_i - reader.local_read_i);
  }

  int writer_ready() {
    return static_cast<int>(writer.local_read_i + queue_buf_size - writer.local_write_i);
  }

  qvalue_t get_unsafe() {
    return q[reader.local_read_i++ & (queue_buf_size - 1)].val;
  }

  void flush_reader() {
    reader.i.store(reader.local_read_i, std::memory_order_release);
  }

  int update_reader() {
    reader.local_write_i = writer.i.load(std::memory_order_acquire);
    return reader_ready();
  }

  void put_unsafe(qvalue_t val) {
    q[writer.local_write_i++ & (queue_buf_size - 1)].val = val;
  }

  void flush_writer() {
    writer.i.store(writer.local_write_i, std::memory_order_release);
  }

  int update_writer() {
    writer.local_read_i = reader.i.load(std::memory_order_acquire);
    return writer_ready();
  }

  int wait_reader() {
    Backoff backoff;
    int res = 0;
    while (res == 0) {
      backoff.next();
      res = update_reader();
    }
    return res;
  }

  qvalue_t get_noflush() {
    if (!reader_empty()) {
      return get_unsafe();
    }

    Backoff backoff;
    while (true) {
      backoff.next();
      if (update_reader()) {
        return get_unsafe();
      }
    }
  }

  qvalue_t get() {
    qvalue_t res = get_noflush();
    flush_reader();
    return res;
  }

  void put_noflush(qvalue_t val) {
    if (!writer_empty()) {
      put_unsafe(val);
      return;
    }
    if (!update_writer()) {
      std::fprintf(stderr, "put strong failed\n");
      std::exit(0);
    }
    put_unsafe(val);
  }

  void put(qvalue_t val) {
    put_noflush(val);
    flush_writer();
  }

  void destroy() {
  }
};

#if TD_LINUX
class BufferedFdQueue {
  int fd;
  std::atomic<int> wait_flag{0};
  BufferQueue q;
  char pad[64];

 public:
  void init() {
    q.init();
    fd = eventfd(0, 0);
    (void)pad[0];
  }
  void put(qvalue_t value) {
    q.put(value);
    td::int64 x = 1;
    __sync_synchronize();
    if (wait_flag.load(MODE)) {
      auto len = write(fd, &x, sizeof(x));
      CHECK(len == sizeof(x));
    }
  }
  void put_noflush(qvalue_t value) {
    q.put_noflush(value);
  }
  void flush_writer() {
    q.flush_writer();
    td::int64 x = 1;
    __sync_synchronize();
    if (wait_flag.load(MODE)) {
      auto len = write(fd, &x, sizeof(x));
      CHECK(len == sizeof(x));
    }
  }
  void flush_reader() {
    q.flush_reader();
  }

  qvalue_t get_unsafe_flush() {
    qvalue_t res = q.get_unsafe();
    q.flush_reader();
    return res;
  }

  qvalue_t get_unsafe() {
    return q.get_unsafe();
  }

  int wait_reader() {
    int res = 0;
    Backoff backoff;
    while (res == 0 && backoff.next()) {
      res = q.update_reader();
    }
    if (res != 0) {
      return res;
    }

    td::int64 x;
    wait_flag.store(1, MODE);
    __sync_synchronize();
    while (!(res = q.update_reader())) {
      auto len = read(fd, &x, sizeof(x));
      CHECK(len == sizeof(x));
      __sync_synchronize();
    }
    wait_flag.store(0, MODE);
    return res;
  }

  qvalue_t get() {
    if (!q.reader_empty()) {
      return get_unsafe_flush();
    }

    Backoff backoff;
    while (backoff.next()) {
      if (q.update_reader()) {
        return get_unsafe_flush();
      }
    }

    td::int64 x;
    wait_flag.store(1, MODE);
    __sync_synchronize();
    while (!q.update_reader()) {
      auto len = read(fd, &x, sizeof(x));
      CHECK(len == sizeof(x));
      __sync_synchronize();
    }
    wait_flag.store(0, MODE);
    return get_unsafe_flush();
  }
  void destroy() {
    q.destroy();
    close(fd);
  }
};

class FdQueue {
  int fd;
  std::atomic<int> wait_flag{0};
  VarQueue q;
  char pad[64];

 public:
  void init() {
    q.init();
    fd = eventfd(0, 0);
    (void)pad[0];
  }
  void put(qvalue_t value) {
    q.put(value);
    td::int64 x = 1;
    __sync_synchronize();
    if (wait_flag.load(MODE)) {
      auto len = write(fd, &x, sizeof(x));
      CHECK(len == sizeof(x));
    }
  }
  qvalue_t get() {
    // td::int64 x;
    // auto len = read(fd, &x, sizeof(x));
    // CHECK(len == sizeof(x));
    // return q.get();

    Backoff backoff;
    qvalue_t res = -1;
    do {
      res = q.try_get();
    } while (res == -1 && backoff.next());
    if (res != -1) {
      q.acquire();
      return res;
    }

    td::int64 x;
    wait_flag.store(1, MODE);
    __sync_synchronize();
    // std::fprintf(stderr, "!\n");
    // while (res == -1 && read(fd, &x, sizeof(x)) == sizeof(x)) {
    // res = q.try_get();
    //}
    do {
      __sync_synchronize();
      res = q.try_get();
    } while (res == -1 && read(fd, &x, sizeof(x)) == sizeof(x));
    q.acquire();
    wait_flag.store(0, MODE);
    return res;
  }
  void destroy() {
    q.destroy();
    close(fd);
  }
};
#endif

class SemBackoffQueue {
  sem_t sem;
  VarQueue q;

 public:
  void init() {
    q.init();
    sem_init(&sem, 0, 0);
  }

  void put(qvalue_t value) {
    q.put(value);
    sem_post(&sem);
  }

  qvalue_t get() {
    Backoff backoff;
    int sem_flag = -1;
    do {
      sem_flag = sem_trywait(&sem);
    } while (sem_flag != 0 && backoff.next());
    if (sem_flag != 0) {
      sem_wait(&sem);
    }
    return q.get();
  }

  void destroy() {
    q.destroy();
    sem_destroy(&sem);
  }
};

class SemCheatQueue {
  sem_t sem;
  VarQueue q;

 public:
  void init() {
    q.init();
    sem_init(&sem, 0, 0);
  }

  void put(qvalue_t value) {
    q.put(value);
    sem_post(&sem);
  }

  qvalue_t get() {
    Backoff backoff;
    qvalue_t res = -1;
    do {
      res = q.try_get();
    } while (res == -1 && backoff.next());
    sem_wait(&sem);
    if (res != -1) {
      q.acquire();
      return res;
    }
    return q.get();
  }

  void destroy() {
    q.destroy();
    sem_destroy(&sem);
  }
};

template <class QueueT>
class QueueBenchmark2 : public td::Benchmark {
  QueueT client, server;
  int connections_n, queries_n;

  int server_active_connections;
  int client_active_connections;
  std::vector<td::int64> server_conn;
  std::vector<td::int64> client_conn;

 public:
  explicit QueueBenchmark2(int connections_n = 1) : connections_n(connections_n) {
  }

  std::string get_description() const override {
    return "QueueBenchmark2";
  }

  void start_up() override {
    client.init();
    server.init();
  }

  void tear_down() override {
    client.destroy();
    server.destroy();
  }

  void server_process(qvalue_t value) {
    int no = value & 0x00FFFFFF;
    int co = static_cast<int>(static_cast<unsigned int>(value) >> 24);
    // std::fprintf(stderr, "-->%d %d\n", co, no);
    if (co < 0 || co >= connections_n || no != server_conn[co]++) {
      std::fprintf(stderr, "%d %d\n", co, no);
      std::fprintf(stderr, "expected %d %lld\n", co, static_cast<long long>(server_conn[co] - 1));
      std::fprintf(stderr, "Server BUG\n");
      while (true) {
      }
    }
    // std::fprintf(stderr, "no = %d/%d\n", no, queries_n);
    // std::fprintf(stderr, "answer: %d %d\n", no, co);

    client.writer_put(value);
    client.writer_flush();
    if (no + 1 >= queries_n) {
      server_active_connections--;
    }
  }

  void *server_run(void *) {
    server_conn = std::vector<td::int64>(connections_n);
    server_active_connections = connections_n;

    while (server_active_connections > 0) {
      int cnt = server.reader_wait();
      if (cnt == 0) {
        std::fprintf(stderr, "ERROR!\n");
        std::exit(0);
      }
      while (cnt-- > 0) {
        server_process(server.reader_get_unsafe());
        server.reader_flush();
      }
      // client.writer_flush();
      server.reader_flush();
    }
    return nullptr;
  }

  void client_process(qvalue_t value) {
    int no = value & 0x00FFFFFF;
    int co = static_cast<int>(static_cast<unsigned int>(value) >> 24);
    // std::fprintf(stderr, "<--%d %d\n", co, no);
    if (co < 0 || co >= connections_n || no != client_conn[co]++) {
      std::fprintf(stderr, "%d %d\n", co, no);
      std::fprintf(stderr, "expected %d %lld\n", co, static_cast<long long>(client_conn[co] - 1));
      std::fprintf(stderr, "BUG\n");
      while (true) {
      }
      std::exit(0);
    }
    if (no + 1 < queries_n) {
      // std::fprintf(stderr, "query: %d %d\n", no + 1, co);
      server.writer_put(value + 1);
      server.writer_flush();
    } else {
      client_active_connections--;
    }
  }

  void *client_run(void *) {
    client_conn = std::vector<td::int64>(connections_n);
    client_active_connections = connections_n;
    if (queries_n >= (1 << 24)) {
      std::fprintf(stderr, "Too big queries_n\n");
      std::exit(0);
    }

    for (int i = 0; i < connections_n; i++) {
      server.writer_put(static_cast<qvalue_t>(i) << 24);
    }
    server.writer_flush();

    while (client_active_connections > 0) {
      int cnt = client.reader_wait();
      if (cnt == 0) {
        std::fprintf(stderr, "ERROR!\n");
        std::exit(0);
      }
      while (cnt-- > 0) {
        client_process(client.reader_get_unsafe());
        client.reader_flush();
      }
      // server.writer_flush();
      client.reader_flush();
    }
    // system("cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    return nullptr;
  }

  static void *client_run_gateway(void *arg) {
    return static_cast<QueueBenchmark2 *>(arg)->client_run(nullptr);
  }

  static void *server_run_gateway(void *arg) {
    return static_cast<QueueBenchmark2 *>(arg)->server_run(nullptr);
  }

  void run(int n) override {
    pthread_t client_thread_id;
    pthread_t server_thread_id;

    queries_n = (n + connections_n - 1) / connections_n;

    pthread_create(&client_thread_id, nullptr, client_run_gateway, this);
    pthread_create(&server_thread_id, nullptr, server_run_gateway, this);

    pthread_join(client_thread_id, nullptr);
    pthread_join(server_thread_id, nullptr);
  }
};

template <class QueueT>
class QueueBenchmark : public td::Benchmark {
  QueueT client, server;
  const int connections_n;
  int queries_n;

 public:
  explicit QueueBenchmark(int connections_n = 1) : connections_n(connections_n) {
  }

  std::string get_description() const override {
    return "QueueBenchmark";
  }

  void start_up() override {
    client.init();
    server.init();
  }

  void tear_down() override {
    client.destroy();
    server.destroy();
  }

  void *server_run(void *) {
    std::vector<td::int64> conn(connections_n);
    int active_connections = connections_n;
    while (active_connections > 0) {
      qvalue_t value = server.get();
      int no = value & 0x00FFFFFF;
      int co = static_cast<int>(value >> 24);
      // std::fprintf(stderr, "-->%d %d\n", co, no);
      if (co < 0 || co >= connections_n || no != conn[co]++) {
        std::fprintf(stderr, "%d %d\n", co, no);
        std::fprintf(stderr, "expected %d %lld\n", co, static_cast<long long>(conn[co] - 1));
        std::fprintf(stderr, "Server BUG\n");
        while (true) {
        }
      }
      // std::fprintf(stderr, "no = %d/%d\n", no, queries_n);
      client.put(value);
      if (no + 1 >= queries_n) {
        active_connections--;
      }
    }
    return nullptr;
  }

  void *client_run(void *) {
    std::vector<td::int64> conn(connections_n);
    if (queries_n >= (1 << 24)) {
      std::fprintf(stderr, "Too big queries_n\n");
      std::exit(0);
    }
    for (int i = 0; i < connections_n; i++) {
      server.put(static_cast<qvalue_t>(i) << 24);
    }
    int active_connections = connections_n;
    while (active_connections > 0) {
      qvalue_t value = client.get();
      int no = value & 0x00FFFFFF;
      int co = static_cast<int>(value >> 24);
      // std::fprintf(stderr, "<--%d %d\n", co, no);
      if (co < 0 || co >= connections_n || no != conn[co]++) {
        std::fprintf(stderr, "%d %d\n", co, no);
        std::fprintf(stderr, "expected %d %lld\n", co, static_cast<long long>(conn[co] - 1));
        std::fprintf(stderr, "BUG\n");
        while (true) {
        }
        std::exit(0);
      }
      if (no + 1 < queries_n) {
        server.put(value + 1);
      } else {
        active_connections--;
      }
    }
    // system("cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    return nullptr;
  }

  void *client_run2(void *) {
    std::vector<td::int64> conn(connections_n);
    if (queries_n >= (1 << 24)) {
      std::fprintf(stderr, "Too big queries_n\n");
      std::exit(0);
    }
    for (int query = 0; query < queries_n; query++) {
      for (int i = 0; i < connections_n; i++) {
        server.put((static_cast<td::int64>(i) << 24) + query);
      }
      for (int i = 0; i < connections_n; i++) {
        qvalue_t value = client.get();
        int no = value & 0x00FFFFFF;
        int co = static_cast<int>(value >> 24);
        // std::fprintf(stderr, "<--%d %d\n", co, no);
        if (co < 0 || co >= connections_n || no != conn[co]++) {
          std::fprintf(stderr, "%d %d\n", co, no);
          std::fprintf(stderr, "expected %d %lld\n", co, static_cast<long long>(conn[co] - 1));
          std::fprintf(stderr, "BUG\n");
          while (true) {
          }
          std::exit(0);
        }
      }
    }
    // system("cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    return nullptr;
  }

  static void *client_run_gateway(void *arg) {
    return static_cast<QueueBenchmark *>(arg)->client_run(nullptr);
  }

  static void *server_run_gateway(void *arg) {
    return static_cast<QueueBenchmark *>(arg)->server_run(nullptr);
  }

  void run(int n) override {
    pthread_t client_thread_id;
    pthread_t server_thread_id;

    queries_n = (n + connections_n - 1) / connections_n;

    pthread_create(&client_thread_id, nullptr, client_run_gateway, this);
    pthread_create(&server_thread_id, nullptr, server_run_gateway, this);

    pthread_join(client_thread_id, nullptr);
    pthread_join(server_thread_id, nullptr);
  }
};

template <class QueueT>
class RingBenchmark : public td::Benchmark {
  enum { QN = 504 };

  struct Thread {
    int int_id;
    pthread_t id;
    QueueT queue;
    Thread *next;
    char pad[64];

    void *run() {
      qvalue_t value;
      // std::fprintf(stderr, "start %d\n", int_id);
      do {
        int cnt = queue.reader_wait();
        CHECK(cnt == 1);
        value = queue.reader_get_unsafe();
        queue.reader_flush();

        next->queue.writer_put(value - 1);
        next->queue.writer_flush();
      } while (value >= QN);
      return nullptr;
    }
  };

  Thread q[QN];

 public:
  static void *run_gateway(void *arg) {
    return static_cast<Thread *>(arg)->run();
  }

  void start_up() override {
    for (int i = 0; i < QN; i++) {
      q[i].int_id = i;
      q[i].queue.init();
      q[i].next = &q[(i + 1) % QN];
    }
  }

  void tear_down() override {
    for (int i = 0; i < QN; i++) {
      q[i].queue.destroy();
    }
  }

  void run(int n) override {
    for (int i = 0; i < QN; i++) {
      pthread_create(&q[i].id, nullptr, run_gateway, &q[i]);
    }

    std::fprintf(stderr, "run %d\n", n);
    if (n < 1000) {
      n = 1000;
    }
    q[0].queue.writer_put(n);
    q[0].queue.writer_flush();

    for (int i = 0; i < QN; i++) {
      pthread_join(q[i].id, nullptr);
    }
  }
};

void test_queue() {
  std::vector<td::thread> threads;
  constexpr size_t threads_n = 100;
  std::vector<td::MpscPollableQueue<int>> queues(threads_n);
  for (auto &q : queues) {
    q.init();
  }
  for (size_t i = 0; i < threads_n; i++) {
    threads.emplace_back([&q = queues[i]] {
      while (true) {
        auto got = q.reader_wait_nonblock();
        while (got-- > 0) {
          q.reader_get_unsafe();
        }
        q.reader_get_event_fd().wait(1000);
      }
    });
  }

  while (true) {
    td::usleep_for(100);
    for (int i = 0; i < 5; i++) {
      queues[td::Random::fast(0, threads_n - 1)].writer_put(1);
    }
  }
}

int main() {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(DEBUG));
  //test_queue();
#define BENCH_Q2(Q, N)                      \
  std::fprintf(stderr, "!%s %d:\t", #Q, N); \
  td::bench(QueueBenchmark2<Q>(N));
#define BENCH_Q(Q, N)                      \
  std::fprintf(stderr, "%s %d:\t", #Q, N); \
  td::bench(QueueBenchmark<Q>(N));

#define BENCH_R(Q)                   \
  std::fprintf(stderr, "%s:\t", #Q); \
  td::bench(RingBenchmark<Q>());
  // TODO: yield makes it extremely slow. Yet some backoff may be necessary.
  //  BENCH_R(SemQueue);
  //  BENCH_R(td::PollQueue<qvalue_t>);

  BENCH_Q2(td::PollQueue<qvalue_t>, 1);
  BENCH_Q2(td::MpscPollableQueue<qvalue_t>, 1);
  BENCH_Q2(td::PollQueue<qvalue_t>, 100);
  BENCH_Q2(td::MpscPollableQueue<qvalue_t>, 100);
  BENCH_Q2(td::PollQueue<qvalue_t>, 10);
  BENCH_Q2(td::MpscPollableQueue<qvalue_t>, 10);

  BENCH_Q(VarQueue, 1);
  // BENCH_Q(FdQueue, 1);
  // BENCH_Q(BufferedFdQueue, 1);
  BENCH_Q(PipeQueue, 1);
  BENCH_Q(SemCheatQueue, 1);
  BENCH_Q(SemQueue, 1);

  // BENCH_Q2(td::PollQueue<qvalue_t>, 100);
  // BENCH_Q2(td::PollQueue<qvalue_t>, 10);
  // BENCH_Q2(td::PollQueue<qvalue_t>, 4);
  // BENCH_Q2(td::InfBackoffQueue<qvalue_t>, 100);

  // BENCH_Q2(td::InfBackoffQueue<qvalue_t>, 1);
  // BENCH_Q(SemCheatQueue, 1);

  // BENCH_Q(BufferedFdQueue, 100);
  // BENCH_Q(BufferedFdQueue, 10);

  // BENCH_Q(BufferQueue, 4);
  // BENCH_Q(BufferQueue, 100);
  // BENCH_Q(BufferQueue, 10);
  // BENCH_Q(BufferQueue, 1);

  return 0;
}
