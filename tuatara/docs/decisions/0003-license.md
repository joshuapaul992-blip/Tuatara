# 0003: MPL 2.0

**Status:** Accepted

## Decision

Tuatara is licensed under the Mozilla Public License 2.0, the license of UXP.
Third-party code in the tree keeps its own license; the list is in
`toolkit/content/license.html`.

## Rules for new files

- Every new source file starts with the MPL 2.0 notice (Exhibit A), for
  example in Python:

  ```
  # This Source Code Form is subject to the terms of the Mozilla Public
  # License, v. 2.0. If a copy of the MPL was not distributed with this
  # file, You can obtain one at http://mozilla.org/MPL/2.0/.
  ```

- Don't add the "Incompatible With Secondary Licenses" notice, so the code
  stays combinable with GPL software.
- Don't commit Microsoft binaries, install media or other files we may not
  redistribute. API baselines store only export names and ordinals.
