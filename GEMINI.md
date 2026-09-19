# Project Development Guidelines

## 1. Initial Setup (Mandatory Reading)
Whenever you start a new conversation or task in this repository, you **MUST** immediately read both the `.dev_context/DEVELOPER_HANDOFF.md` and `.dev_context/CONVERSATION_HISTORY.md` files using the `view_file` tool. 
These documents contain the entire technical history of the project, including critical BLE quirks, protocol reverse-engineering, hardware limits (ESP32 vs ESP32-C3), the UI architecture, and the step-by-step evolution of our chats. 
You cannot work on this project without reading them first.

## 2. End of Task (Mandatory Backup)
After every major completion, feature implementation, or significant architectural change, you **MUST**:
1. Update the `.dev_context/DEVELOPER_HANDOFF.md` file with any new technical context, bugs solved, pinouts assigned, or decisions made during the session.
2. Commit the changes (along with your code) to git.
3. Push the changes to the GitHub repository to serve as a backup and continuous developer handoff.
