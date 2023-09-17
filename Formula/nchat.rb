class Nchat < Formula
  desc "Terminal-based Telegram / WhatsApp Client for Linux and Macos"
  homepage "https://github.com/d99kris/nchat"
  url "https://github.com/d99kris/nchat/archive/refs/tags/v3.60.tar.gz"
  sha256 "8a382603d5e9ef1942f796995dc4652d9e9665d1dbcf55349461e81ecebf4bca"
  license "MIT"

  depends_on "ccache"
  depends_on "cmake" => :build
  depends_on "go"
  depends_on "gperf"
  depends_on "help2man"
  depends_on "libmagic"
  depends_on "ncurses"
  depends_on "openssl"
  depends_on "readline"
  depends_on "sqlite"

  def install 
    mkdir "build" do 
      system "cmake", "..", *std_cmake_args
      system "make", "-s"
      system "make", "install"
    end
  end

  test do 
    system "#{bin}/nchat", "--version"
  end
end
