# Contributing

This is a guide to help you get started contributing to the DataStax C/C++ Driver.

## Forking and Branching

Fork the driver on [GitHub](https://github.com/apache/cassandra-cpp-driver) and clone
your fork locally.

```bash
$ git clone https://github.com/username/cassandra-cpp-driver.git
$ cd cassandra-cpp-driver
$ git remote add upstream https://github.com/apache/cassandra-cpp-driver
```

Features that require breaking API or ABI changes should be reserved for major
releases and will probably not be considered for minor or patch releases.
For features and big changes consider using a feature branch.

```bash
$ git checkout -b awesome-new-feature
```

Make sure to `git rebase` to keep your branch up-to-date.

```bash
$ git fetch upstream
$ git rebase upstream
```

To avoid duplication of work it's important to check [JIRA] to see if a bug
or feature has already been fixed or added. [JIRA] should also be used to
report issues and to track new features.

## Coding Style

As a rule of thumb, follow the style of the current code. In general, the code
follows the conventions and style defined in the [Google C++ Style Guide].
Differences and other key points are outlined below.

### General

* Code should be cross platform and cross architecture or be explicitly defined for all
supported platforms/architectures.
* Comments shouldn't explain the obvious.
* Comments should be constructed using proper sentences with punctuation.

### Whitespace

* Use two space indentation and with no tabs.
* Lines should be wrapped at 80 characters, but this is loose right now. Maybe
  this can be bumped to 100.
* Lines should have no trailing whitespace and use unix-style (LF) line
  endings.

### Conventions

* Exposed names should be prefixed with `cass_` or `CASS_`.
* Code in `cassandra.h` should conform to C89 syntax.
* Use [early exits] to simplfy code.

### Style

* Use proper case for structs, classes, and enums e.g. `SomeClass`.
* Use lower case with underscores for variables and functions e.g.
  `do_something_with_object`.
* Member variables should have a trailing `_` character
* The `if, while, switch` etc. keywords should have a space between it and the
  opening parenthesis.
```cpp
if (some_condition) {
  // Do something
} else if (some_other_condition) {
  // Do something else
}

while (some_condition) {
  // Do something
}
```
* Line up constructor initialization list on the colon.
```cpp
SomeObject()
  : member1_(1)
  , member2_(0.0f)
  // ...
  , memberN_("foo") {}
```
* Comments should begin with a space.
```cpp
// This is comment
```

## Testing

Tests should be included for both bug fixes and features. Tests should be added
to `test/unit_tests/src` or `tests/integration_tests/src`. Unit tests should
be used for testing components or changes that don't require Cassandra.

This [testing guide] is useful for understanding the structure of the tests
and how to run them.


## Committing

Make sure that your commit has a proper commit message with a succinct, and
descriptive first line followed by a blank line. The message
should also contain a description of the change and why it was required.

## Pull Requests

Once your changes look good and you've crafted a proper commit message push
your branch and create a pull request.

```bash
$ git push origin awesome-new-feature
```

Go to your fork (https://github.com/username/cassandra-cpp-driver), select the branch with
your changes and click 'Pull Request'. Fill out the pull request and submit.

Your changes should usually be reviewed within a few days, otherwise we'll try
to give you a timeline. If your pull request requires fixes or changes please
submit them in a new commit. These commits will be squashed before your pull request
is merged.

[JIRA]: https://issues.apache.org/jira/issues/?jql=project%20%3D%20CASSCPP%20ORDER%20BY%20key%20DESC
[Google C++ Style Guide]: https://google.github.io/styleguide/cppguide.html
[early exits]: http://llvm.org/docs/CodingStandards.html#use-early-exits-and-continue-to-simplify-code
[testing guide]: https://docs.datastax.com/en/developer/cpp-driver/latest/topics/testing
