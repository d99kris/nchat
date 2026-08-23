Contributing
============
nchat is feature-complete and in maintenance mode. Read
[Project Scope](/doc/SCOPE.md) first, it describes which contributions will be
considered and which will not. This document covers the practical steps.

Reporting a bug
---------------
Search the [issues](https://github.com/d99kris/nchat/issues) first, to check
if it's already reported. If not, open a new issue of type `Bug report` with a
clear title and description, steps to reproduce, your nchat version and OS,
and a copy of `~/.config/nchat/log.txt`.

Report security vulnerabilities as issues as well.

Suggesting a feature
--------------------
Open a new
[Ideas discussion](https://github.com/d99kris/nchat/discussions/new?category=ideas).
Each request is evaluated by the maintainer and assigned a category, applied
as a label:

- **pr-welcome** - good feature to add, implementation contribution welcome
- **planned** - good feature to add, maintainer is planning to implement it
- **considering** - pending more upvotes or clarification on scope
- **not-planned** - no plans to add and PRs will not be reviewed / merged

Please wait for a **pr-welcome** label, and an agreed approach and rough
size, before writing any code. Feature pull requests without one will be
closed unreviewed. Refer to [Project Scope](/doc/SCOPE.md) for evaluation
criteria.

Submitting a pull request
-------------------------
**Bug fixes**: Ensure an issue is reported and reference its number in the PR.

**New features**: Ensure a feature request is reported and labelled
**pr-welcome** (see above) and reference its number in the PR.

- One bug or one feature per pull request. Do not bundle unrelated
  refactoring or reformatting.
- Build and test your changes (`./make.sh build`)
- There is no formal style guide but the style is close to
  [Google's C++ Style](https://google.github.io/styleguide/cppguide.html).
- Describe the problem and the solution in the pull request description.

Approved pull requests are typically merged via an integration branch for
follow-up fixes (e.g. version bump), before being merged to the master branch.

Using AI tools
--------------
Allowed, but you are responsible for the result: build it, test it, and be
able to explain how it works. PRs that appear untested are closed without
detailed review.

Asking a question
-----------------
Use the
[Q&A discussions](https://github.com/d99kris/nchat/discussions/categories/q-a)
section.
