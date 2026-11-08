# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys

if __package__ in (None, ""):
    # Run as `python3 tuatara/tools/apiaudit ...`: make the package importable.
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from apiaudit.cli import main
else:
    from .cli import main

sys.exit(main())
