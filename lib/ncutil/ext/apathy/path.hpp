/******************************************************************************
 * Copyright (c) 2013 Dan Lecocq
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#ifndef APATHY__PATH_HPP
#define APATHY__PATH_HPP

/* C++ includes */
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <istream>
#include <sstream>
#include <iostream>
#include <iterator>

/* C includes */
#include <glob.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/* A class for path manipulation */
namespace apathy {
    class Path {
    public:
        /* This is the separator used on this particular system */
#ifdef __MSDOS__
        #error "Platforms using backslashes not yet supported"
#else
        static const char separator = '/';
#endif
        /* A class meant to contain path segments */
        struct Segment {
            /* The actual string segment */
            std::string segment;

            Segment(std::string s=""): segment(s) {}

            friend std::istream& operator>>(std::istream& stream, Segment& s) {
                return std::getline(stream, s.segment, separator);
            }
        };

        /**********************************************************************
         * Constructors
         *********************************************************************/

        /* Default constructor
         *
         * Points to current directory */
        Path(const std::string& pPath=""): path(pPath) {}

        /* Our generalized constructor.
         *
         * This enables all sorts of type promotion (like int -> Path) for
         * arguments into all the functions below. Anything that
         * std::stringstream can support is implicitly supported as well
         *
         * @param p - path to construct */
        template <class T>
        Path(const T& p);

        /**********************************************************************
         * Operators
         *********************************************************************/
        /* Checks if the paths are exactly the same */
        bool operator==(const Path& other) { return path == other.path; }

        /* Check if the paths are not exactly the same */
        bool operator!=(const Path& other) { return ! (*this == other); }

        /* Append the provided segment to the path as a directory. This is the
         * same as append(segment)
         *
         * @param segment - path segment to add to this path */
        Path& operator<<(const Path& segment);

        /* Append the provided segment to the path as a directory. This is the
         * same as append(segment). Returns a /new/ path object rather than a
         * reference.
         *
         * @param segment - path segment to add to this path */
        Path operator+(const Path& segment) const;

        /* Check if the two paths are equivalent
         *
         * Two paths are equivalent if they point to the same resource, even if
         * they are not exact string matches
         *
         * @param other - path to compare to */
        bool equivalent(const Path& other);

        /* Return a string version of this path */
        std::string string() const { return path; }

        /* Return the name of the file */
        std::string filename() const;

        /* Return the extension of the file */
        std::string extension() const;

        /* Return a path object without the extension */
        Path stem() const;

        /**********************************************************************
         * Manipulations
         *********************************************************************/

        /* Append the provided segment to the path as a directory. Alias for
         * `operator<<`
         *
         * @param segment - path segment to add to this path */
        Path& append(const Path& segment);

        /* Evaluate the provided path relative to this path. If the second path
         * is absolute, then return the second path.
         *
         * @param rel - path relative to this path to evaluate */
        Path& relative(const Path& rel);

        /* Move up one level in the directory structure */
        Path& up();

        /* Turn this into an absolute path
         *
         * If the path is already absolute, it has no effect. Otherwise, it is
         * evaluated relative to the current working directory */
        Path& absolute();

        /* Sanitize this path
         *
         * This...
         *
         * 1) replaces runs of consecutive separators with a single separator
         * 2) evaluates '..' to refer to the parent directory, and
         * 3) strips out '/./' as referring to the current directory
         *
         * If the path was absolute to begin with, it will be absolute 
         * afterwards. If it was a relative path to begin with, it will only be
         * converted to an absolute path if it uses enough '..'s to refer to
         * directories above the current working directory */
        Path& sanitize();

        /* Make this path a directory
         *
         * If this path does not have a trailing directory separator, add one.
         * If it already does, this does not affect the path */
        Path& directory();

        /* Trim this path of trailing separators, up to the leading separator.
         * For example, on *nix systems:
         *
         *   assert(Path("///").trim() == "/");
         *   assert(Path("/foo//").trim() == "/foo");
         */
        Path& trim();

        /**********************************************************************
         * Copiers
         *********************************************************************/

        /* Return parent path
         *
         * Returns a new Path object referring to the parent directory. To
         * move _this_ path to the parent directory, use the `up` function */
        Path parent() const { return Path(Path(*this).up()); }

        /**********************************************************************
         * Member Utility Methods
         *********************************************************************/

        /* Returns a vector of each of the path segments in this path */
        std::vector<Segment> split() const;

        /**********************************************************************
         * Type Tests
         *********************************************************************/
        /* Is the path an absolute path? */
        bool is_absolute() const;

        /* Does the path have a trailing slash? */
        bool trailing_slash() const;

        /* Does this path exist?
         *
         * Returns true if the path can be `stat`d */
        bool exists() const;

        /* Is this path an existing file?
         *
         * Only returns true if the path has stat.st_mode that is a regular
         * file */
        bool is_file() const;

        /* Is this path an existing directory?
         *
         * Only returns true if the path has a stat.st_mode that is a
         * directory */
        bool is_directory() const;

        /* How large is this file?
         *
         * Returns the file size in bytes. If the file doesn't exist, it
         * returns 0 */
        size_t size() const;

        /**********************************************************************
         * Static Utility Methods
         *********************************************************************/

        /* Return a brand new path as the concatenation of the two provided
         * paths
         *
         * @param a - first part of the path to join
         * @param b - second part of the path to join
         */
        static Path join(const Path& a, const Path& b);

        /* Return a branch new path as the concatenation of each segments
         *
         * @param segments - the path segments to concatenate
         */
        static Path join(const std::vector<Segment>& segments);

        /* Current working directory */
        static Path cwd();

        /* Create a file if one does not exist
         *
         * @param p - path to create
         * @param mode - mode to create with */
        static bool touch(const Path& p, mode_t mode=0777);

        /* Move / rename a file
         *
         * @param source - original path
         * @param dest - new path
         * @param mkdirs - recursively make any needed directories? */
        static bool move(const Path& source, const Path& dest,
            bool mkdirs=false);

        /* Remove a file
         *
         * @param path - path to remove */
        static bool rm(const Path& path);

        /* Recursively make directories
         *
         * @param p - path to recursively make
         * @returns true if it was able to, false otherwise */
        static bool makedirs(const Path& p, mode_t mode=0777);

        /* Recursively remove directories
         *
         * @param p - path to recursively remove */
        static bool rmdirs(const Path& p, bool ignore_errors=false);

        /* List all the paths in a directory
         *
         * @param p - path to list items for */
        static std::vector<Path> listdir(const Path& p);

        /* Returns all matching globs
         *
         * @param pattern - the glob pattern to match */
        static std::vector<Path> glob(const std::string& pattern);

        /* So that we can write paths out to ostreams */
        friend std::ostream& operator<<(std::ostream& stream, const Path& p) {
            return stream << p.path;
        }
    private:
        /* Our current path */
        std::string path;
    };

    /* Constructor */
    template <class T>
    inline Path::Path(const T& p): path("") {
        std::stringstream ss;
        ss << p;
        path = ss.str();
    }

    /**************************************************************************
     * Operators
     *************************************************************************/
    inline Path& Path::operator<<(const Path& segment) {
        return append(segment);
    }

    inline Path Path::operator+(const Path& segment) const {
        Path result(path);
        result.append(segment);
        return result;
    }

    inline bool Path::equivalent(const Path& other) {
        /* Make copies of both paths, sanitize, and ensure they're equal */
        return Path(path).absolute().sanitize() ==
               Path(other).absolute().sanitize();
    }

    inline std::string Path::filename() const {
        size_t pos = path.rfind(separator);
        if (pos != std::string::npos) {
            return path.substr(pos + 1);
        }
        return "";
    }

    inline std::string Path::extension() const {
        /* Make sure we only look in the filename, and not the path */
        std::string name = filename();
        size_t pos = name.rfind('.');
        if (pos != std::string::npos) {
            return name.substr(pos + 1);
        }
        return "";
    }

    inline Path Path::stem() const {
        size_t sep_pos = path.rfind(separator);
        size_t dot_pos = path.rfind('.');
        if (dot_pos == std::string::npos) {
            return Path(*this);
        }

        if (sep_pos == std::string::npos || sep_pos < dot_pos) {
            return Path(path.substr(0, dot_pos));
        } else {
            return Path(*this);
        }
    }

    /**************************************************************************
     * Manipulators
     *************************************************************************/
    inline Path& Path::append(const Path& segment) {
        /* First, check if the last character is the separator character.
         * If not, then append one and then the segment. Otherwise, just
         * the segment */
        if (!trailing_slash()) {
            path.push_back(separator);
        }
        path.append(segment.path);
        return *this;
    }

    inline Path& Path::relative(const Path& rel) {
        if (!rel.is_absolute()) {
            return append(rel);
        } else {
            operator=(rel);
            return *this;
        }
    }

    inline Path& Path::up() {
        /* Make sure we turn this into an absolute url if it's not already
         * one */
        if (path.size() == 0) {
            path = "..";
            return directory();
        }
        
        append("..").sanitize();
        if (path.size() == 0) {
            return *this;
        }
        return directory();
    }

    inline Path& Path::absolute() {
        /* If the path doesn't begin with our separator, then it's not an
         * absolute path, and should be appended to the current working
         * directory */
        if (!is_absolute()) {
            /* Join our current working directory with the path */
            operator=(join(cwd(), path));
        }
        return *this;
    }

    inline Path& Path::sanitize() {
        /* Split the path up into segments */
        std::vector<Segment> segments(split());
        /* We may have to test this repeatedly, so let's check once */
        bool relative = !is_absolute();

        /* Now, we'll create a new set of segments */
        std::vector<Segment> pruned;
        for (size_t pos = 0; pos < segments.size(); ++pos) {
            /* Skip over empty segments and '.' */
            if (segments[pos].segment.size() == 0 ||
                segments[pos].segment == ".") {
                continue;
            }

            /* If there is a '..', then pop off a parent directory. However, if
             * the path was relative to begin with, if the '..'s exceed the
             * stack depth, then they should be appended to our path. If it was
             * absolute to begin with, and we reach root, then '..' has no
             * effect */
            if (segments[pos].segment == "..") {
                if (relative) {
                    if (pruned.size() && pruned.back().segment != "..") {
                        pruned.pop_back();
                    } else {
                        pruned.push_back(segments[pos]);
                    }
                } else if (pruned.size()) {
                    pruned.pop_back();
                }
                continue;
            }

            pruned.push_back(segments[pos]);
        }
        
        bool was_directory = trailing_slash();
        if (!relative) {
            path = std::string(1, separator) + Path::join(pruned).path;
            if (was_directory) {
                return directory();
            }
            return *this;
        }

        /* It was a relative path */
        path = Path::join(pruned).path;
        if (path.length() && was_directory) {
            return directory();
        }
        return *this;
    }

    inline Path& Path::directory() {
        trim();
        path.push_back(separator);
        return *this;
    }

    inline Path& Path::trim() {
        if (path.length() == 0) { return *this; }

        size_t p = path.find_last_not_of(separator);
        if (p != std::string::npos) {
            path.erase(p + 1, path.size());
        } else {
            path = "";
        }
        return *this;
    }

    /**************************************************************************
     * Member Utility Methods
     *************************************************************************/

    /* Returns a vector of each of the path segments in this path */
    inline std::vector<Path::Segment> Path::split() const {
        std::stringstream stream(path);
        std::istream_iterator<Path::Segment> start(stream);
        std::istream_iterator<Path::Segment> end;
        std::vector<Path::Segment> results(start, end);
        if (trailing_slash()) {
            results.push_back(Path::Segment(""));
        }
        return results;
    }

    /**************************************************************************
     * Tests
     *************************************************************************/
    inline bool Path::is_absolute() const {
        return path.size() && path[0] == separator;
    }

    inline bool Path::trailing_slash() const {
        return path.size() && path[path.length() - 1] == separator;
    }

    inline bool Path::exists() const {
        struct stat buf;
        if (stat(path.c_str(), &buf) != 0) {
            return false;
        }
        return true;
    }

    inline bool Path::is_file() const {
        struct stat buf;
        if (stat(path.c_str(), &buf) != 0) {
            return false;
        } else {
            return S_ISREG(buf.st_mode);
        }
    }

    inline bool Path::is_directory() const {
        struct stat buf;
        if (stat(path.c_str(), &buf) != 0) {
            return false;
        } else {
            return S_ISDIR(buf.st_mode);
        }
    }

    inline size_t Path::size() const {
        struct stat buf;
        if (stat(path.c_str(), &buf) != 0) {
            return 0;
        } else {
            return buf.st_size;
        }
    }

    /**************************************************************************
     * Static Utility Methods
     *************************************************************************/
    inline Path Path::join(const Path& a, const Path& b) {
        Path p(a);
        p.append(b);
        return p;
    }

    inline Path Path::join(const std::vector<Segment>& segments) {
        std::string path;
        /* Now, we'll go through the segments, and join them with
         * separator */
        std::vector<Segment>::const_iterator it(segments.begin());
        for(; it != segments.end(); ++it) {
            path += it->segment;
            if (it + 1 != segments.end()) {
                path += std::string(1, separator);
            }
        }
        return Path(path);
    }

    inline Path Path::cwd() {
        Path p;

        char * buf = getcwd(NULL, 0);
        if (buf != NULL) {
            p = std::string(buf);
            free(buf);
        } else {
            perror("cwd");
        }

        /* Ensure this is a directory */
        p.directory();
        return p;
    }

    inline bool Path::touch(const Path& p, mode_t mode) {
        int fd = open(p.path.c_str(), O_RDONLY | O_CREAT, mode);
        if (fd == -1) {
            makedirs(p);
            fd = open(p.path.c_str(), O_RDONLY | O_CREAT, mode);
            if (fd == -1) {
                return false;
            }
        }

        if (close(fd) == -1) {
            perror("touch close");
            return false;
        }

        return true;
    }

    inline bool Path::move(const Path& source, const Path& dest,
        bool mkdirs) {
        int result = rename(source.path.c_str(), dest.path.c_str());
        if (result == 0) {
            return true;
        }

        /* Otherwise, there was an error */
        if (errno == ENOENT && mkdirs) {
            makedirs(dest.parent());
            return rename(source.path.c_str(), dest.path.c_str()) == 0;
        }

        return false;
    }

    inline bool Path::rm(const Path& path) {
        if (remove(path.path.c_str()) != 0) {
            perror("Remove");
            return false;
        }
        return true;
    }

    inline bool Path::makedirs(const Path& p, mode_t mode) {
        /* We need to make a copy of the path, that's an absolute path */
        Path abs = Path(p).absolute();

        /* Now, we'll try to make the directory / ensure it exists */
        if (mkdir(abs.string().c_str(), mode) == 0) {
            return true;
        }

        /* Otherwise, there was an error. There are some errors that
         * may be recoverable */
        if (errno == EEXIST) {
            return abs.is_directory();
        } else if(errno == ENOENT) {
            /* We'll need to try to recursively make this directory. We
             * don't need to worry about reaching the '/' path, and then
             * getting to this point, because / always exists */
            makedirs(abs.parent(), mode);
            if (mkdir(abs.string().c_str(), mode) == 0) {
                return true;
            } else {
                perror("makedirs");
                return false;
            }
        } else {
            perror("makedirs");
        }

        /* If it's none of these cases, then it's one of unrecoverable
         * errors described in mkdir(2) */
        return false;
    }

    inline bool Path::rmdirs(const Path& p, bool ignore_errors) {
        /* If this path isn't a file, then complain */
        if (!p.is_directory()) {
            return false;
        }

        /* First, we list out all the members of the path, and anything
         * that's a directory, we rmdirs(...) it. If it's a file, then we
         * remove it */
        std::vector<Path> subdirs(listdir(p));
        std::vector<Path>::iterator it(subdirs.begin());
        for (; it != subdirs.end(); ++it) {
            if (it->is_directory() && !rmdirs(*it) && !ignore_errors) {
                std::cout << "Failed rmdirs " << it->string() << std::endl;
            } else if (it->is_file() &&
                remove(it->path.c_str()) != 0 && !ignore_errors) {
                std::cout << "Failed remove " << it->string() << std::endl;
            }
        }

        /* Lastly, try to remove the directory itself */
        bool result = (remove(p.path.c_str()) == 0);
        errno = 0;
        return result;
    }

    /* List all the paths in a directory
     *
     * @param p - path to list items for */
    inline std::vector<Path> Path::listdir(const Path& p) {
        Path base(p);
        base.absolute();
        std::vector<Path> results;
        DIR* dir = opendir(base.string().c_str());
        if (dir == NULL) {
            /* If there was an error, return an empty vector */
            return results;
        }

        /* Otherwise, go through everything */
        for (dirent* ent = readdir(dir); ent != NULL; ent = readdir(dir)) {
            Path cpy(base);

            /* Skip the parent directory listing */
            if (!strcmp(ent->d_name, "..")) {
                continue;
            }

            /* Skip the self directory listing */
            if (!strcmp(ent->d_name, ".")) {
                continue;
            }

            cpy.relative(ent->d_name);
            results.push_back(cpy);
        }

        errno = 0;
        closedir(dir);
        return results;
    }

    inline std::vector<Path> Path::glob(const std::string& pattern) {
        /* First, we need a glob_t, and then we'll look at the results */
        glob_t globbuf;
        if (::glob(pattern.c_str(), 0, NULL, &globbuf) != 0) {
            /* Then there was an error */
            return std::vector<Path>();
        }

        std::vector<Path> results;
        for (std::size_t i = 0; i < globbuf.gl_pathc; ++i) {
            results.push_back(globbuf.gl_pathv[i]);
        }
        return results;
    }
}

#endif
