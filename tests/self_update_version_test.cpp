#include "update/version.h"
#include "playden_build_commit.h"
#include <cstdio>
#include <cstdlib>
using namespace playden::update;
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #value); std::exit(1); } } while(false)
int main() {
  CHECK(ParseVersion("0.2.0") == (Version{0, 2, 0}));
  CHECK(ParseVersion("v0.3.10") == (Version{0, 3, 10}));
  CHECK(ParseVersion("00.002.000") == (Version{0, 2, 0}));  // param.json contentVersion
  CHECK(ParseVersion("01.000.000") == (Version{1, 0, 0}));
  for (const char* bad : {"", "v", "0.2", "0.2.0.1", "0.2.0-beta", "0..1", "a.b.c", " 0.2.0", "0.2.0 ", "1234567.0.0", "vv0.2.0"})
    CHECK(!ParseVersion(bad));
  CHECK((Version{0, 2, 0}) < (Version{0, 2, 1}));
  CHECK((Version{0, 2, 9}) < (Version{0, 10, 0}));
  CHECK((Version{0, 99, 99}) < (Version{1, 0, 0}));
  CHECK(!((Version{1, 0, 0}) < (Version{1, 0, 0})));
  CHECK((Version{0, 3, 10}).ToString() == "0.3.10");
  // The build's own version comes from CMake.
  if (!CurrentVersion().nightly()) CHECK(!NightlyBase());  // only a nightly records one
  CHECK(ParseVersion(PLAYDEN_VERSION) && CurrentVersion() == *ParseVersion(PLAYDEN_VERSION));
  // And its commit from git, when the source is a checkout.
  CHECK(BuildCommit() == PLAYDEN_BUILD_COMMIT);
  CHECK(BuildCommit().empty() || BuildCommit().find_first_not_of("0123456789abcdef") == std::string::npos);
  CHECK(BuildHasLocalChanges() == (PLAYDEN_BUILD_LOCAL_CHANGES != 0));
  CHECK(BuildVersionText() == VersionWithBuild(CurrentVersion(), BuildCommit(), BuildHasLocalChanges()));
  // What Playden sends: the commit as build metadata, ".dirty" with local changes, nothing without a commit.
  CHECK(VersionWithBuild(Version{0, 11, 0}, "3f9a0c1d2e", false) == "0.11.0+3f9a0c1d2e");
  CHECK(VersionWithBuild(Version{0, 11, 0}, "3f9a0c1d2e", true) == "0.11.0+3f9a0c1d2e.dirty");
  CHECK(VersionWithBuild(Version{0, 11, 0}, "", true) == "0.11.0");
  CHECK(VersionWithBuild(*ParseVersion("0.11.0-nightly.20261107.1"), "3f9a0c1d2e", false) ==
        "0.11.0-nightly.20261107.1+3f9a0c1d2e");
  const auto beta = ParseVersion("v0.7.1-beta.1");
  CHECK(beta);
  CHECK(beta->ToString() == "0.7.1-beta.1");
  CHECK(ParseVersion(beta->ToString()) == beta);
  CHECK(*ParseVersion("0.7.0") < *beta);
  CHECK(*beta < *ParseVersion("0.7.1-beta.2"));
  CHECK(*ParseVersion("0.7.1-beta.999999") < *ParseVersion("0.7.1"));
  CHECK(*beta != *ParseVersion("0.7.1"));
  for (const char* bad : {"0.7.1-rc.1", "0.7.1-beta.0", "0.7.1-beta.01", "0.7.1-beta.1000000",
                          "0.7.1-beta.-1", "0.7.1-beta.1+meta", "0.7.1+meta", "0.7.1-beta.1.2"})
    CHECK(!ParseVersion(bad));
  // Nightly builds: before every beta and the stable release of their version, after the one before.
  const auto nightly = ParseVersion("0.8.0-nightly.20261027.2");
  CHECK(nightly && nightly->nightly() && nightly->nightly_date == 20261027 && nightly->nightly_build == 2);
  CHECK(nightly->ToString() == "0.8.0-nightly.20261027.2");
  CHECK(ParseVersion(nightly->ToString()) == nightly);
  CHECK(*ParseVersion("0.7.2") < *nightly);
  CHECK(*ParseVersion("0.8.0-nightly.20261027.1") < *nightly);
  CHECK(*nightly < *ParseVersion("0.8.0-nightly.20261028.1"));
  CHECK(*nightly < *ParseVersion("0.8.0-beta.1"));
  CHECK(*nightly < *ParseVersion("0.8.0"));
  CHECK(!(*ParseVersion("0.8.0") < *nightly) && !(*ParseVersion("0.8.0-beta.1") < *nightly));
  CHECK(*nightly < *ParseVersion("0.8.1-nightly.20200101.1"));
  CHECK(*nightly != *ParseVersion("0.8.0-nightly.20261027.3"));
  // A trailing b is this fork's build of that upstream version. It is not SemVer, and a second
  // build of the same upstream version is not a different version.
  const auto upstream = ParseVersion("0.11.0");
  const auto fork = ParseVersion("0.11.0b");
  const auto again = ParseVersion("v0.11.0b");
  CHECK(upstream && fork && again && *fork == *again && fork->fork_build && !upstream->fork_build);
  CHECK(fork->ToString() == "0.11.0b" && ParseVersion(fork->ToString()) == fork);
  CHECK(*upstream < *fork && !(*fork < *upstream));
  const Version numeric{fork->major, fork->minor, fork->patch};
  CHECK(numeric == *ParseVersion("00.011.000") && numeric != *fork);
  CHECK(*fork < *ParseVersion("0.12.0"));
  CHECK(*fork < *ParseVersion("0.12.0b"));
  CHECK(*ParseVersion("0.12.0") < *ParseVersion("0.12.0b"));
  CHECK(!(*fork < *again) && !(*again < *fork));
  const auto beta_upstream = ParseVersion("0.11.0-beta.1");
  const auto beta_fork = ParseVersion("0.11.0-beta.1b");
  const auto beta_next = ParseVersion("0.11.0-beta.2b");
  CHECK(beta_upstream && beta_fork && beta_next);
  CHECK(*beta_upstream < *beta_fork && *beta_fork < *beta_next && *beta_next < *fork);
  CHECK(ParseVersion("0.11.0-nightly.20261109.1b")->fork_build);
  CHECK(*ParseVersion("0.11.0-nightly.20261109.1") < *ParseVersion("0.11.0-nightly.20261109.1b"));
  for (const char* bad : {"0.11.0B", "0.11.0b2", "0.11.0bb", "0.11.0b-beta.1", "0.11.0-beta.1B"})
    CHECK(!ParseVersion(bad));
  for (const char* bad : {"0.8.0-nightly", "0.8.0-nightly.20261027", "0.8.0-nightly.20261027.0", "0.8.0-nightly.20261027.01",
                          "0.8.0-nightly.20261027.1000", "0.8.0-nightly.2026102.1", "0.8.0-nightly.20261327.1",
                          "0.8.0-nightly.20261000.1", "0.8.0-nightly.020261027.1", "0.8.0-nightly.20261027.1.1",
                          "0.8.0-beta.1-nightly.20261027.1", "0.8.0-nightly.20261027.1-beta.1"})
    CHECK(!ParseVersion(bad));
  return 0;
}
