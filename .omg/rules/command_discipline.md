---
description: Strict command discipline, database safety, and git tool safety constraints
globs: ["**"]
alwaysApply: true
---

# Command Discipline & Tool Safety Rules

1. **Strict Command Sequencing**:
   - Never run extra, ad-hoc, exploratory, or out-of-order commands.
   - Execute strictly in the linear order requested by the operator.

2. **Database Safety Lock**:
   - Never execute unprompted or unrequested database commands, loops, or batch queries against MariaDB (`tw_world`, `tw_char`, `tw_logon`, `tw_logs`).
   - Only perform database operations when explicitly directed by the operator.

3. **Git & Tool Safety**:
   - Never execute destructive git commands (`git checkout -- <file>`, `git restore`, `git reset`, `git clean`) on working files unless explicitly ordered by the operator.
   - Always use native file editing tools (`replace_file_content`, `write_to_file`) for modifying or reverting file contents.
