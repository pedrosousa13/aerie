"""The scheduled bump job: shape checks on .github/workflows/bump-helium.yml."""

import re
import unittest
from pathlib import Path

WORKFLOW = (Path(__file__).resolve().parent.parent / ".github" / "workflows" / "bump-helium.yml").read_text()


class BumpWorkflowTest(unittest.TestCase):
    def test_runs_on_a_schedule_and_by_hand_only(self):
        self.assertIn("schedule:", WORKFLOW)
        self.assertIn("workflow_dispatch:", WORKFLOW)
        self.assertNotIn("pull_request", WORKFLOW)

    def test_actions_are_pinned_to_a_commit(self):
        for ref in re.findall(r"uses:\s*(\S+)", WORKFLOW):
            self.assertRegex(ref, r"@[0-9a-f]{40}$")

    def test_default_token_is_read_only_and_not_kept_in_the_checkout(self):
        self.assertRegex(WORKFLOW, r"permissions:\n  contents: read\n")
        self.assertIn("persist-credentials: false", WORKFLOW)

    def test_nothing_from_upstream_is_pasted_into_a_script(self):
        # Values the job reads from upstream reach the shell only through env, never through ${{ }} in run.
        for run in re.findall(r"run: \|\n((?:          .*\n|\n)+)", WORKFLOW):
            self.assertNotIn("${{", run)

    def test_does_nothing_without_a_new_release(self):
        self.assertIn("./aerie bump --check", WORKFLOW)
        self.assertIn("steps.look.outputs.found == 'true'", WORKFLOW)

    def test_one_pull_request_per_release_open_or_closed(self):
        self.assertIn("--state all", WORKFLOW)
        self.assertIn("bump-helium-$TAG", WORKFLOW)

    def test_a_failing_patch_check_opens_a_draft(self):
        self.assertIn("--draft", WORKFLOW)

    def test_a_branch_left_by_an_earlier_failed_run_does_not_block_the_push(self):
        # A branch without a pull request (push worked, `gh pr create` did not) is replaced, not rejected.
        self.assertRegex(WORKFLOW, r"git push --force ")

    def test_package_index_is_refreshed_before_installing(self):
        self.assertLess(WORKFLOW.index("apt-get update"), WORKFLOW.index("apt-get install"))

    def test_pull_request_is_opened_with_the_maintainers_token(self):
        self.assertIn("secrets.BUMP_PR_TOKEN", WORKFLOW)


if __name__ == "__main__":
    unittest.main()
