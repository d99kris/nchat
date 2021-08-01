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


#define CATCH_CONFIG_MAIN

#include <catch.hpp>
#include <algorithm>

/* Internal libraries */
#include "path.hpp"

using namespace apathy;

TEST_CASE("path", "Path functionality works as advertised") {
    SECTION("cwd", "And equivalent vs ==") {
        Path cwd(Path::cwd());
        Path empty("");
        REQUIRE(cwd != empty);
        REQUIRE(cwd.equivalent(empty));
        REQUIRE(empty.equivalent(cwd));
        REQUIRE(cwd.is_absolute());
        REQUIRE(!empty.is_absolute());
        REQUIRE(empty.absolute() == cwd);
        REQUIRE(Path() == "");
    }

    SECTION("operator=", "Make sure assignment works as expected") {
        Path cwd(Path::cwd());
        Path empty("");
        REQUIRE(cwd != empty);
        empty = cwd;
        REQUIRE(cwd == empty);
    }

    SECTION("operator+=", "Make sure operator<< works correctly") {
        Path root("/");
        root << "hello" << "how" << "are" << "you";
        REQUIRE(root.string() == "/hello/how/are/you");

        /* It also needs to be able to accept things like floats, ints, etc. */
        root = Path("/");
        root << "hello" << 5 << "how" << 3.14 << "are";
        REQUIRE(root.string() == "/hello/5/how/3.14/are");
    }

    SECTION("operator+", "Make sure operator+ works correctly") {
        Path root("foo/bar");
        REQUIRE((root + "baz").string() == "foo/bar/baz");
    }

    SECTION("trim", "Make sure trim actually strips off separators") {
        Path root("/hello/how/are/you////");
        REQUIRE(root.trim().string() == "/hello/how/are/you");
        root = Path("/hello/how/are/you");
        REQUIRE(root.trim().string() == "/hello/how/are/you");
        root = Path("/hello/how/are/you/");
        REQUIRE(root.trim().string() == "/hello/how/are/you");
    }

    SECTION("directory", "Make sure we can make paths into directories") {
        Path root("/hello/how/are/you");
        REQUIRE(root.directory().string() == "/hello/how/are/you/");
        root = Path("/hello/how/are/you/");
        REQUIRE(root.directory().string() == "/hello/how/are/you/");
        root = Path("/hello/how/are/you//");
        REQUIRE(root.directory().string() == "/hello/how/are/you/");
    }

    SECTION("relative", "Evaluates relative urls correctly") {
        Path a("/hello/how/are/you");
        Path b("foo");
        REQUIRE(a.relative(b).string() == "/hello/how/are/you/foo");
        a = Path("/hello/how/are/you/");
        REQUIRE(a.relative(b).string() == "/hello/how/are/you/foo");
        b = Path("/fine/thank/you");
        REQUIRE(a.relative(b).string() == "/fine/thank/you");
    }

    SECTION("parent", "Make sure we can find the parent directory") {
        Path a("/hello/how/are/you");
        REQUIRE(a.parent().string() == "/hello/how/are/");
        a = Path("/hello/how/are/you");
        REQUIRE(a.parent().parent().string() == "/hello/how/");
        
        /* / is its own parent, at least according to bash:
         *
         *    cd / && cd ..
         */
        a = Path("/");
        REQUIRE(a.parent().string() == "/");

        a = Path("");
        REQUIRE(a.parent() != Path::cwd().parent());
        REQUIRE(a.parent().equivalent(Path::cwd().parent()));

        a = Path("foo/bar");
        REQUIRE(a.parent().parent() == "");
        a = Path("foo/../bar/baz/a/../");
        REQUIRE(a.parent() == "bar/");
    }

    SECTION("makedirs", "Make sure we recursively make directories") {
        Path path("foo");
        REQUIRE(!path.exists());
        path << "bar" << "baz" << "whiz";
        Path::makedirs(path);
        REQUIRE(path.exists());
        REQUIRE(path.is_directory());

        /* Now, we should remove the directories, make sure it's gone. */
        REQUIRE(Path::rmdirs("foo"));
        REQUIRE(!Path("foo").exists());
    }

    SECTION("listdirs", "Make sure we can list directories") {
        Path path("foo");
        path << "bar" << "baz" << "whiz";
        Path::makedirs(path);
        REQUIRE(path.exists());

        /* Now touch some files in this area */
        Path::touch(Path(path).append("a"));
        Path::touch(Path(path).append("b"));
        Path::touch(Path(path).append("c"));

        /* Now list that directory */
        std::vector<Path> files = Path::listdir(path);
        REQUIRE(files.size() == 3);
        /* listdir doesn't enforce any ordering */
        REQUIRE((std::find(files.begin(), files.end(), 
            Path(path).absolute().append("a").string()) != files.end()));
        REQUIRE((std::find(files.begin(), files.end(), 
            Path(path).absolute().append("b").string()) != files.end()));
        REQUIRE((std::find(files.begin(), files.end(), 
            Path(path).absolute().append("c").string()) != files.end()));

        REQUIRE(Path::rmdirs("foo"));
        REQUIRE(!Path("foo").exists());
    }

    SECTION("rm", "Make sure we can remove files we create") {
        REQUIRE(!Path("foo").exists());
        Path::touch("foo");
        REQUIRE( Path("foo").exists());
        Path::rm("foo");
        REQUIRE(!Path("foo").exists());
    }

    SECTION("move", "Make sure we can move files / directories") {
        /* We should be able to move it in the most basic case */
        Path source("foo");
        Path dest("bar");
        REQUIRE(!source.exists());
        REQUIRE(!  dest.exists());
        Path::touch(source);

        REQUIRE(Path::move(source, dest));
        REQUIRE(!source.exists());
        REQUIRE(   dest.exists());

        REQUIRE(Path::rm(dest));
        REQUIRE(!source.exists());
        REQUIRE(!  dest.exists());

        /* And now, when the directory doesn't exist */
        dest = "bar/baz";
        REQUIRE(!dest.parent().exists());
        Path::touch(source);

        REQUIRE(!Path::move(source, dest));
        REQUIRE( Path::move(source, dest, true));
        REQUIRE(!source.exists());
        REQUIRE(   dest.exists());
        Path::rmdirs("bar");
        REQUIRE(!Path("bar").exists());
    }

    SECTION("sanitize", "Make sure we can sanitize a path") {
        Path path("foo///bar/a/b/../c");
        REQUIRE(path.sanitize() == "foo/bar/a/c");

        path = "../foo///bar/a/b/../c";
        REQUIRE(path.sanitize() == "../foo/bar/a/c");

        path = "../../a/b////c";
        REQUIRE(path.sanitize() == "../../a/b/c");

        path = "/../../a/b////c";
        REQUIRE(path.sanitize() == "/a/b/c");

        path = "/./././a/./b/../../c";
        REQUIRE(path.sanitize() == "/c");

        path = "././a/b/c/";
        REQUIRE(path.sanitize() == "a/b/c/");
    }

    SECTION("equivalent", "Make sure equivalent paths work") {
        Path a("foo////a/b/../c/");
        Path b("foo/a/c/");
        REQUIRE(a.equivalent(b));

        a = "../foo/bar/";
        b = Path::cwd().parent().append("foo").append("bar").directory();
        REQUIRE(a.equivalent(b));
    }

    SECTION("split", "Make sure we can get segments out") {
        Path a("foo/bar/baz");
        std::vector<Path::Segment> segments(a.split());
        REQUIRE(segments.size() == 3);
        REQUIRE(segments[0].segment == "foo");
        REQUIRE(segments[1].segment == "bar");
        REQUIRE(segments[2].segment == "baz");

        a = Path("foo/bar/baz/");
        REQUIRE(a.split().size() == 4);

        a = Path("/foo/bar/baz/");
        REQUIRE(a.split().size() == 5);
    }

    SECTION("extension", "Make sure we can accurately get th file extension") {
        /* Works in a basic way */
        REQUIRE(Path("foo/bar/baz.out").extension() == "out");
        /* Gets the outermost extension */
        REQUIRE(Path("foo/bar.baz.out").extension() == "out");
        /* Doesn't take extensions from directories */
        REQUIRE(Path("foo/bar.baz/out").extension() == "");
    }

    SECTION("stem", "Make sure we can get the path stem") {
        /* Works in a basic way */
        REQUIRE(Path("foo/bar/baz.out").stem() == Path("foo/bar/baz"));
        /* Gets the outermost extension */
        REQUIRE(Path("foo/bar.baz.out").stem() == Path("foo/bar.baz"));
        /* Doesn't take extensions from directories */
        REQUIRE(Path("foo/bar.baz/out").stem() == Path("foo/bar.baz/out"));

        /* Can be used to successively pop off the extension */
        Path a("foo.bar.baz.out");
        a = a.stem(); REQUIRE(a == Path("foo.bar.baz"));
        a = a.stem(); REQUIRE(a == Path("foo.bar"));
        a = a.stem(); REQUIRE(a == Path("foo"));
        a = a.stem(); REQUIRE(a == Path("foo"));
    }

    SECTION("glob", "Make sure glob works") {
        /* We'll touch a bunch of files to work with */
        Path::makedirs("foo");
        Path::touch("foo/bar");
        Path::touch("foo/bar2");
        Path::touch("foo/bar3");
        Path::touch("foo/baz");
        Path::touch("foo/bazzy");
        Path::touch("foo/foo");

        /* Make sure we can get it to work in a few basic ways */
        REQUIRE(Path::glob("foo/*"   ).size() == 6);
        REQUIRE(Path::glob("foo/b*"  ).size() == 5);
        REQUIRE(Path::glob("foo/baz*").size() == 2);
        REQUIRE(Path::glob("foo/ba?" ).size() == 2);

        /* Now, we should remove the directories, make sure it's gone. */
        REQUIRE(Path::rmdirs("foo"));
        REQUIRE(!Path("foo").exists());
    }
}
