Apathy Path Manipulation
========================
I find myself occasionally needing to do path manipulation in C++, and while
[Boost.Filesystem](http://www.boost.org/doc/libs/1_52_0/libs/filesystem/doc/index.htm)
is a useful tool, my qualm with it is that it requires Boost to be installed
on the target system. For some cases, this is too large a requirement, and so
I have often wanted this library.

Installation
============
`Apathy` is a header-only library, and so all you need to do is to include it
in your code (this works particularly well with
[git submodules](http://git-scm.com/book/en/Git-Tools-Submodules)):

```C++
#include <apathy/path.hpp>
```

It imports a single member `Path` in the `apathy` namespace.

Usage
=====
Most of the path manipulators return a reference to the current path, so that
they can be chained:

```C++
/* Check if ./foo/bar exists */
Path::cwd().relative("foo").relative("bar").exists();
/* Make a sanitized absolute directory for ./foo///../a/b */
Path("./foo///../a/b").absolute().sanitize();
```

Operators
=========
Path objects support the operators `==`, `!=`, `<<` (which appends to the path)
and `=`:

```C++
/* Makes sure they're exact matches */
Path("./foo") == "./foo";
Path("./foo") != "foo";

/* Appends to the path */
Path p("foo");
p << "bar" << 5 << 3.1459 << Path("what");
```

In addition to these comparators, there's also an `equivalent` method that
checks whether the two paths refer to the same resource. To do so, copies of
both are made absolute and sanitized and then a strict string comparison is
made:

```C++
/* These are equivalent, but not equal */
Path a("./foo////a/b/./d/../c");
Path b("foo/a/b/c");

/* These are true */
a.equivalent(b);
b.equivalent(a);
/* This is not */
a == b;
```

Modifiers
=========
All of these methods modify the path they're associated with, and return a
reference to the modified path so that they can be chained:

- `append` -- appends a path segment to the current path:

```C++
/* Create a path to foo/bar/baz
Path p("foo");
std::cout << p.append("bar").append("baz").string() << std::endl;
/* Now p is "foo/bar/baz" */
```

- `relative` -- evaluates one path relative to another. If the second path is
    an absolute path, then updates the object to point to that path:

```C++
/* Gives "foo/bar/whiz" */
Path("foo").relative("bar/whiz");
/* Gives "/bar/whiz" */
Path("foo").relative("/bar/whiz");
```

- `up` -- move to the parent directory:
    
```C++
/* Gives /foo/bar/ */
Path("foo/bar/whiz").up();
```

- `absolute` -- convert the path to an absolute path. If it's a relative path,
    then it's evaluated relative to the current working directory:

```C++
/* Gives <cwd>/foo/bar */
Path("foo/bar").absolute();
/* Gives /foo/bar */
Path("/foo/bar").absolute();
```

- `sanitize` -- clean up repeated separators, evaluate `..` and `.`. If `..` is
    used to exceed the segments in a relative path, it is transformed into an
    absolute path. Otherwise, if it was a relative path, it remains a relative
    path afterwards:

```C++
/* Gives a/b/c/ */
Path("foo/.././a////b/d/../c");
```

- `directory` -- ensure the path has a trailing separator to indicate it's a
    directory:

```C++
/* Gives a/b/c/ */
Path("a/b/c").directory();
```

- `trim` -- removes any trailing separators from the path:

```C++
/* Gives a/b/c */
Path("a/b/c/////").trim();
```

Breaking It Down
================
There are a number of ways to get access to the various components of the path:

- `filename` -- name of the file without any directories
- `extension` -- get a string of the extension of the path (if any)
- `stem` -- get a copy of the path without the extension
- `split` -- each of the directories in the path

Copiers
=======
While the modifiers change the instance itself and return a reference, some
methods return a modified copy of the instance, leaving the original unchanged:

- `copy` -- an easy shorthand for creating a copy of the path object, since
    it's common to chain methods. Consider:

```C++
/* I'd like to leave the original path unchanged */
Path p("foo/bar");
p.copy().absolute();

/* Equivalent to... */
Path(p).absolute();
```

- `parent` -- returns a new path pointing to the parent directory:

```C++
/* Points to foo/bar, leaving original unchanged */
Path p("foo/bar/whiz");
p.parent();
```

Tests
=====
You can run some simple checks about the path:

- `is_absolute` -- if the path is an absolute path
- `trailing_slash` -- if the path has a trailing slash

As well as get information about the filesystem:

- `exists` -- returns true if path can successfully be `stat`-ed
- `is_directory` -- returns true if the path exists and `S_ISDIR`
- `is_file` -- returns true if the path exists and `S_ISREG`

Utility Functions
=================
Lastly, there are a number of utility functions for dealing with paths and the
filesystem:

- `cwd` -- get a path that refers to the current working directory
- `touch` -- update and make sure a file exists
- `makedirs` -- attempt to recursively make a directory
- `rmdirs` -- attempt to recursively remove a directory
- `listdir` -- return a vector of all the paths in the provided directory

Roadmap
=======
The interface is a little bit in flux, but I now need this code in more than
one projects, so it was time for it to move into its own repository.