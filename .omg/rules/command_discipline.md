---
description: Strict command discipline and database safety constraints
globs: ["**"]
alwaysApply: true
---

# Command Discipline & Database Safety Rules

1. **Strict Command Sequencing**:
   - Never run extra, ad-hoc, exploratory, or out-of-order commands.
   - Execute strictly in the linear order requested by the operator.

2. **Database Safety Lock**:
   - Never execute unprompted or unrequested database commands, loops, or batch queries against MariaDB (`tw_world`, `tw_char`, `tw_logon`, `tw_logs`).
   - Only perform database operations when explicitly directed by the operator.
