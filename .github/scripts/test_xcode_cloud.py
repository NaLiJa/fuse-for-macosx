"""Regression checks for release provenance and artifact selection."""

import unittest

from xcode_cloud import notarized_artifact, start_build, verify_run, wait_for_run


class FakeAPI:
    def __init__(self, documents=None, collections=None):
        self.documents = documents or {}
        self.collections = collections or {}
        self.created = None

    def request(self, path, payload=None):
        if payload is not None:
            self.created = payload
            return {"data": {"id": "new-run"}}
        return self.documents[path]

    def items(self, path):
        return iter(self.collections[path])


def run(sha="a" * 40, status="SUCCEEDED", workflow="workflow"):
    return {"relationships": {"workflow": {"data": {"id": workflow}}},
            "attributes": {"sourceCommit": {"commitSha": sha},
                           "executionProgress": "COMPLETE", "completionStatus": status}}


class CloudTests(unittest.TestCase):
    def test_mismatched_commit_or_workflow_cannot_be_released(self):
        for build in (run(sha="b" * 40), run(workflow="other")):
            with self.assertRaises(ValueError):
                verify_run(build, "workflow", "a" * 40)

    def test_failed_build_is_not_downloaded(self):
        api = FakeAPI({"ciBuildRuns/run?include=workflow": {"data": run(status="FAILED")}})
        with self.assertRaises(RuntimeError):
            wait_for_run(api, "run", "workflow", "a" * 40, 10)

    def test_processing_build_can_be_resumed_after_timeout(self):
        build = run()
        build["attributes"].update(executionProgress="RUNNING", completionStatus=None)
        api = FakeAPI({"ciBuildRuns/run?include=workflow": {"data": build}})
        with self.assertRaisesRegex(TimeoutError, "build_run_id=run"):
            wait_for_run(api, "run", "workflow", "a" * 40, 0)

    def test_development_artifact_is_rejected(self):
        api = FakeAPI(collections={"ciBuildRuns/run/actions?limit=200": [{"id": "action"}],
                                  "ciBuildActions/action/artifacts?limit=200": [
                                      {"id": "dev", "attributes": {"fileType": "XCODEBUILD_PRODUCTS"}}]})
        with self.assertRaises(ValueError):
            notarized_artifact(api, "run")

    def test_notarized_artifact_is_selected_instead_of_archive(self):
        signed = {"id": "signed", "attributes": {"fileType": "STAPLED_NOTARIZED_ARCHIVE"}}
        api = FakeAPI(collections={"ciBuildRuns/run/actions?limit=200": [{"id": "action"}],
                                  "ciBuildActions/action/artifacts?limit=200": [
                                      {"id": "archive", "attributes": {"fileType": "ARCHIVE"}}, signed]})
        self.assertEqual(notarized_artifact(api, "run"), signed)

    def test_trigger_uses_requested_tag_not_default_branch(self):
        api = FakeAPI({"ciWorkflows/workflow/repository": {"data": {"id": "repo"}}},
                      {"scmRepositories/repo/gitReferences?limit=200": [
                          {"id": "main", "attributes": {"canonicalName": "refs/heads/main"}},
                          {"id": "tag", "attributes": {"canonicalName": "refs/tags/release"}}]})
        self.assertEqual(start_build(api, "workflow", "refs/tags/release"), "new-run")
        source = api.created["data"]["relationships"]["sourceBranchOrTag"]["data"]
        self.assertEqual(source["id"], "tag")


if __name__ == "__main__":
    unittest.main()
