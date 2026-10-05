# release_notes

The current release notes live one level up, next to the manifest they describe:
[`../RELEASE_NOTES.md`](../RELEASE_NOTES.md).

They are deliberately not duplicated here. The notes quote counts (suite size,
regression steps, requirement classification) that change whenever the evidence
does, and a second copy is how a stale number survives — the documentation audit
(regression step 10) checks the walkthrough and the release notes against
`11_documentation/PROJECT_FACTS.json`, so a stale copy would be caught, but a
single file is simpler to keep true.

Nothing in this directory is a release artifact; the manifest and its audit are.
