# Coding Guidelines

## Branching
- Create a new branch before starting any work. Never commit directly to `main`/`master`.
- Use descriptive, prefixed branch names:
  - `feat/` for new features (e.g., `feat/volume-adjust`)
  - `fix/` for bug fixes (e.g., `fix/screen-flicker`)
  - `refactor/`, `docs/`, `test/`, `chore/` as appropriate
- Keep branches focused on a single concern; split large changes into multiple branches/PRs.

## Code Structure
- Write modular code: each module, class, and function should have a single, well-defined responsibility.
- Keep functions short and focused; prefer pure functions where practical.
- Avoid duplication — extract shared logic into reusable utilities.
- Follow the existing project conventions for naming, file layout, and style. Match the surrounding code rather than introducing new patterns.
- Document non-obvious decisions with comments explaining *why*, not *what*.

## Testing (TDD)
- Write tests **before** implementation:
  1. Write a failing test that captures the desired behavior.
  2. Write the minimum code to make it pass.
  3. Refactor while keeping tests green.
- Cover the happy path, edge cases, and error conditions.
- Tests must be deterministic — no reliance on timing, network, or external state unless explicitly mocked.
- Run the full test suite locally before pushing.

## Commits
- Make small, logically atomic commits.
- Write clear commit messages in the imperative mood (e.g., "Add volume slider", not "Added volume slider").
- Reference issue numbers where applicable (e.g., `fix/screen-flicker: debounce resize handler (#142)`).

## Documentation
- Each function, class, and module should have a docstring describing its purpose, inputs, outputs, error handling, and any side effects.
- Update documentation (README, design docs, inline comments) as part of the same PR that implements the change.
- Document public interfaces, expected inputs/outputs, and any non-obvious behavior. For complex logic, include examples or diagrams if helpful.

## Pull Requests
- Open a PR only when:
  - All tests pass locally and in CI.
  - Linters and type checks pass with no new warnings.
  - All known issues for the branch are resolved.
  - The branch is rebased on (or merged with) the latest `main`.
- PR description should include:
  - A summary of what changed and why.
  - How to test/verify the change.
  - Links to related issues or design docs.
- Keep PRs small and reviewable (ideally under ~400 lines of diff).
- When creating a PR, Codex review should be triggered automatically.
- Address review comments with follow-up commits; do not force-push after review has started unless requested.
- After creating a PR, request review by appending `@codex review PR` in the PR body to notify the reviewer.
- Before addressing review feedback, ensure you understand the comment. If unclear, ask for clarification rather than guessing.
- Always verify the review feedback is a valid concern before making changes. If you disagree with the feedback, respectfully explain your reasoning in a comment rather than ignoring it.
- After addressing review feedback, re-request review by commenting `@codex review PR` again to ensure the reviewer knows to re-review the changes.

## Before You Code
- Confirm the requirements and acceptance criteria are clear. Ask for clarification when uncertain rather than guessing.
- For non-trivial work, sketch the approach (data model, interfaces, edge cases) before writing code.
- Before reinventing the wheel, search for existing demo code from the vendor or community first.

## Safety
- Never commit secrets, credentials, or `.env` files.
- Don't run destructive commands (force-push to shared branches, history rewrites, `rm -rf` on shared paths) without explicit confirmation.