# Tests

The foundation has a CTest smoke check that verifies the executable starts and
prints its project identity. Run it with:

```sh
ctest --test-dir build --output-on-failure
```

The PDF corpus lives in `pdfs/`, grouped into normal, AcroForm, static XFA,
dynamic XFA, XFA JavaScript, and malformed documents. These directories are
initially empty; no PDF compatibility is claimed by the smoke check.

For each future fixture, record its source, redistribution license, expected
behavior, and any privacy review. Add documents that expose bugs as regression
fixtures when licensing and privacy permit.
