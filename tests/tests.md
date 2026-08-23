# Testing {#testing}

@brief How Botanik is tested - the in-firmware self-checks and the two host-run
       suites.

Botanik is verified at three levels, from the firmware checking itself to host
suites that drive the real simulator:

- @ref dev_health - **Built-in self-tests and asserts.** The power-on self-test
  that validates structural invariants at every boot, and the debug-only
  `BOTANIK_ASSERT`. These ship inside the firmware.
- @subpage cpptests - **C++ tests.** A native [doctest](https://github.com/doctest/doctest)
  suite over the site's control laws, the POU vocabulary, and the table
  integrity a typo would otherwise break silently.
- @subpage termtests - **Terminal emulation tests.** A pyte-based suite that
  drives the real `botanik_sim` over a PTY and asserts the reconstructed screen
  against golden fixtures.

## Coverage at a glance {#testing_coverage}

Regenerated on every docs build (`scripts/code_charts.py`), broken down by code
area - a companion to the LoC / churn charts on the @ref development page.

@htmlonly
<div class="code-charts">
  <figure>
    <img src="code_asserts.svg" alt="Self-checks by area"/>
    <figcaption>Self-checks by area: BOTANIK_ASSERT call sites and POST structural checks</figcaption>
  </figure>
  <figure>
    <img src="code_tests.svg" alt="Tests by area"/>
    <figcaption>Unit-test cases and pyte screen-tests by area</figcaption>
  </figure>
</div>
@endhtmlonly
