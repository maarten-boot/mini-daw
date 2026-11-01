# Test cases — mini-daw

| ID | Spec | Description | Expected result |
|----|------|-------------|-----------------|
| T-UI-01 | SPEC-UI-01 | Start the app with no config file | White background, black text |
| T-UI-02 | SPEC-UI-02 | Compute the contrast ratio of every colour pair in the default theme | All ≥ 4.5:1 |
| T-UI-03 | SPEC-UI-03 | Switch the preset AA → AAA → Max at runtime | Colours update right away; all pairs ≥ 7:1, then 21:1 |
| T-UI-04 | SPEC-UI-04 | Set `min_contrast = 7.0` in the config file, then start the app | All pairs ≥ 7:1 |
| T-UI-05 | SPEC-UI-05 | Set a custom minimum of 3.0 | Rejected with a message; 4.5:1 stays in effect |
| T-UI-06 | SPEC-UI-05 | Set a custom minimum of 10.0 | All pairs ≥ 10:1 |
| T-UI-07 | SPEC-UI-06 | Theme has a pair at 5:1, minimum set to AAA | That pair is adjusted to ≥ 7:1 |
