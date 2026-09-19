# Project Development Guidelines

## 1. Initial Setup (Mandatory Reading)
Whenever you start a new conversation or task in this repository, you **MUST** immediately read both the `.dev_context/DEVELOPER_HANDOFF.md` and `.dev_context/CONVERSATION_HISTORY.md` files using the `view_file` tool. 
These documents contain the entire technical history of the project, including critical BLE quirks, protocol reverse-engineering, hardware limits (ESP32 vs ESP32-C3), the UI architecture, and the step-by-step evolution of our chats. 
You cannot work on this project without reading them first.

## 2. End of Task & Version Control
After every major completion, feature implementation, or significant architectural change, you **MUST**:
1. Update the `.dev_context/DEVELOPER_HANDOFF.md` file with any new technical context, bugs solved, pinouts assigned, or decisions made during the session.

**Git Commit/Push Rule:** 
* **DO NOT** automatically commit or push every time you write code.
* **ONLY** commit and push changes to git when the code/system is fully successful and verified working, OR when the user explicitly asks you to commit and push.
