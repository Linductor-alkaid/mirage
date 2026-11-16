"""DEC-139/140/141: private headless services and real CLI experience files.

No model, credential, display, clipboard or user settings are used. The test
owner bounds and reaps its subprocesses; product work stays on Executor.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
import uuid


class Wire:
    def __init__(self, endpoint):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.settimeout(10)
        self.sock.connect(str(endpoint))
        self.sequence = 0

    def read(self, count):
        result = b""
        while len(result) < count:
            data = self.sock.recv(count - len(result))
            if not data:
                raise RuntimeError("private service closed IPC")
            result += data
        return result

    def call(self, op, **fields):
        self.sequence += 1
        payload = json.dumps(dict(v=1, id=self.sequence, op=op, **fields)).encode()
        self.sock.sendall(struct.pack("<I", len(payload)) + payload)
        size = struct.unpack("<I", self.read(4))[0]
        if size > 1048576:
            raise RuntimeError("private service exceeded frame budget")
        response = json.loads(self.read(size))
        assert response["id"] == self.sequence
        return response


def run(args):
    with tempfile.TemporaryDirectory(prefix="mirage-experience-") as temp:
        root = Path(temp)
        env = os.environ.copy()
        for key in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS"):
            env.pop(key, None)
        for key in ("XDG_RUNTIME_DIR", "XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"):
            directory = root / key.lower()
            directory.mkdir(mode=0o700)
            env[key] = str(directory)
        settings = root / "settings.json"
        settings.write_text(json.dumps(dict(schema=1, model=dict(enabled=False))))
        evidence = root / "evidence.txt"
        evidence.write_text("private experience fixture\n")
        endpoint = root / "service.sock"
        wire = None
        service_command = [str(args.service), "--socket", str(endpoint), "--read-root", str(root),
                           "--config", str(settings), "--no-recovery", "--perm", "filesystem.read=allow"]
        with (root / "service.log").open("w") as log:
            service = subprocess.Popen(
                service_command,
                env=env, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 15
                while wire is None:
                    if service.poll() is not None:
                        raise RuntimeError("private service failed to start")
                    try:
                        wire = Wire(endpoint)
                    except OSError:
                        if time.monotonic() >= deadline:
                            raise TimeoutError("private service readiness")
                        time.sleep(0.02)

                def call(op, **fields):
                    response = wire.call(op, **fields)
                    assert response["ok"], (op, response.get("error", {}).get("code"))
                    return response

                def cli(*command, **kwargs):
                    kwargs.setdefault("stderr", subprocess.PIPE)
                    return subprocess.run([str(args.cli), *command, "--socket", str(endpoint)],
                                          env=env, text=True, timeout=20, **kwargs)

                before = call("workflow.learning.status")["learning"]
                status = cli("workflow", "learning", stdout=subprocess.PIPE)
                assert status.returncode == 0, status.stderr
                learning = json.loads(status.stdout)["learning"]
                assert learning == before
                assert learning["enabled"] is True and 0 < learning["file_bytes"] <= 64 * 1024 * 1024
                assert learning["limit_bytes"] == 64 * 1024 * 1024
                assert learning["reserve_bytes"] == 4 * 1024 * 1024
                assert set(learning) == {"enabled", "file_bytes", "limit_bytes", "reserve_bytes", "peak_admitted_file_bytes"}
                assert cli("workflow", "learning", "unexpected").returncode != 0
                assert call("workflow.learning.status")["learning"] == before

                def start(parameters, workflow_id=None, policy="strict"):
                    deadline = time.monotonic() + 10
                    while True:
                        response = wire.call("workflow.run", workflow_id=workflow_id or source_id,
                                             parameters=parameters, policy=policy)
                        if response["ok"]:
                            return response["run_id"]
                        if "capacity" not in response.get("error", {}).get("message", ""):
                            raise RuntimeError("unexpected workflow admission rejection")
                        if time.monotonic() >= deadline:
                            raise TimeoutError("workflow admission")
                        time.sleep(0.02)

                def terminal(run_id):
                    deadline = time.monotonic() + 10
                    while time.monotonic() < deadline:
                        for record in call("workflow.runs")["runs"]:
                            if record["run_id"] == run_id and record["state"] in ("completed", "failed", "cancelled"):
                                return record["state"]
                        time.sleep(0.02)
                    raise TimeoutError("workflow terminal state")

                source_id, target_id = uuid.uuid4().hex, uuid.uuid4().hex
                definition = dict(
                    schema_version=dict(major=1, minor=0), workflow_id=source_id,
                    name="read fixture", parameters=[dict(name="file_path", type="string",
                        required=False, default=str(evidence))],
                    steps=[dict(step_id=uuid.uuid4().hex, kind="tool_call", arguments={
                        "tool": "desktop.filesystem.read_text", "path": {"$param": "file_path"}})],
                    default_policy="strict", allowed_policies=["strict", "recoverable", "dry_run"])
                source = call("workflow.publish", definition=definition)
                run_id = start({})
                assert terminal(run_id) == "completed"
                catalog_before = call("workflow.list")["workflows"]
                runs_before = call("workflow.runs")["runs"]
                command = ["workflow", "experience", run_id, "--workflow-id", target_id,
                           "--name", "review candidate", "--summary", "one observed run"]
                exported = cli(*command, stdout=subprocess.PIPE)
                assert exported.returncode == 0, "CLI export failed"
                draft = json.loads(exported.stdout)
                assert draft["workflow_id"] == target_id
                assert draft["name"] == "review candidate"
                assert draft["steps"][0]["arguments"]["path"] == str(evidence)
                digest = hashlib.sha256(json.dumps(draft, sort_keys=True, ensure_ascii=False,
                    separators=(",", ":")).encode()).hexdigest()
                assert f"candidate {digest}\n" in exported.stderr
                assert f"source_run {run_id}\n" in exported.stderr
                assert f"source_version {source['digest']}\n" in exported.stderr
                assert str(evidence) not in exported.stderr
                repeated = cli(*command, stdout=subprocess.PIPE)
                assert repeated.returncode == 0 and repeated.stdout == exported.stdout
                assert call("workflow.list")["workflows"] == catalog_before
                assert call("workflow.runs")["runs"] == runs_before
                assert not wire.call("workflow.get", workflow_id=target_id)["ok"]

                for rejected_source in (source["dry_run_id"], uuid.uuid4().hex):
                    rejected = cli("workflow", "experience", rejected_source, "--workflow-id",
                                   target_id, "--name", "rejected", stdout=subprocess.PIPE)
                    assert rejected.returncode != 0 and not rejected.stdout
                failed_id = start(dict(file_path=str(root / "missing.txt")))
                assert terminal(failed_id) == "failed"
                rejected = cli("workflow", "experience", failed_id, "--workflow-id",
                               target_id, "--name", "failed", stdout=subprocess.PIPE)
                assert rejected.returncode != 0 and not rejected.stdout
                bad_usage = cli("workflow", "experience", run_id, stdout=subprocess.PIPE)
                assert bad_usage.returncode == 2 and not bad_usage.stdout
                if Path("/dev/full").exists():
                    with open("/dev/full", "w") as full:
                        assert cli(*command, stdout=full).returncode != 0
                        assert cli(*command, stdout=subprocess.PIPE, stderr=full).returncode != 0

                # User review is represented by explicit inspection above,
                # then a separate invocation of the existing publish gate.
                candidate_file = root / "candidate.workflow.json"
                candidate_file.write_text(exported.stdout)
                published = cli("workflow", "publish", "--file", str(candidate_file), stdout=subprocess.PIPE)
                assert published.returncode == 0 and f"workflow {target_id}\n" in published.stdout
                assert call("workflow.get", workflow_id=target_id)["definition"] == draft
                # DEC-140: two real successful executions propose a parameter;
                # the review artifact preserves both observed values.
                second_file = root / "second.txt"
                second_file.write_text("second private fixture\n")
                second_id = start(dict(file_path=str(second_file)))
                assert terminal(second_id) == "completed"
                induced_id = uuid.uuid4().hex
                multi = ["workflow", "experience", run_id, "--also-run", second_id,
                         "--workflow-id", induced_id, "--name", "parameter candidate"]
                before = (call("workflow.list")["workflows"], call("workflow.runs")["runs"])
                review_output = cli(*multi, "--review", stdout=subprocess.PIPE)
                assert review_output.returncode == 0
                review = json.loads(review_output.stdout)["experience"]
                assert review["validation"] == "candidate"
                assert [s["source_run_id"] for s in review["sources"]] == [run_id, second_id]
                proposal = review["parameter_candidates"]
                assert len(proposal) == 1
                assert proposal[0]["name"] == "file_path" and proposal[0]["pointer"] == "/path"
                assert proposal[0]["observed_values"] == [str(evidence), str(second_file)]
                assert str(evidence) not in review_output.stderr and str(second_file) not in review_output.stderr
                compiled = cli(*multi, stdout=subprocess.PIPE)
                assert compiled.returncode == 0
                parameterized = json.loads(compiled.stdout)
                assert parameterized == review["definition"]
                assert parameterized["steps"][0]["arguments"]["path"] == {"$param": "file_path"}
                assert next(p for p in parameterized["parameters"] if p["name"] == "file_path")["default"] == str(evidence)
                again = cli(*multi, "--review", stdout=subprocess.PIPE)
                assert again.returncode == 0 and json.loads(again.stdout)["experience"] == review
                assert (call("workflow.list")["workflows"], call("workflow.runs")["runs"]) == before
                identical_id = start({})
                assert terminal(identical_id) == "completed"
                unchanged = cli("workflow", "experience", run_id, "--also-run", identical_id,
                                "--workflow-id", induced_id, "--name", "same values", "--review", stdout=subprocess.PIPE)
                assert unchanged.returncode == 0
                assert json.loads(unchanged.stdout)["experience"]["parameter_candidates"] == []
                for bad_source in (run_id, failed_id, source["dry_run_id"], uuid.uuid4().hex):
                    rejected = cli("workflow", "experience", run_id, "--also-run", bad_source,
                                   "--workflow-id", induced_id, "--name", "rejected", stdout=subprocess.PIPE)
                    assert rejected.returncode != 0 and not rejected.stdout
                recoverable_id = start({}, policy="recoverable")
                assert terminal(recoverable_id) == "completed"
                rejected = cli("workflow", "experience", run_id, "--also-run", recoverable_id,
                               "--workflow-id", induced_id, "--name", "mixed policy", stdout=subprocess.PIPE)
                assert rejected.returncode != 0 and not rejected.stdout
                revised = dict(definition, name="new source version")
                call("workflow.publish", definition=revised)
                revised_id = start({})
                assert terminal(revised_id) == "completed"
                rejected = cli("workflow", "experience", run_id, "--also-run", revised_id,
                               "--workflow-id", induced_id, "--name", "mixed version", stdout=subprocess.PIPE)
                assert rejected.returncode != 0 and not rejected.stdout
                candidate_file.write_text(compiled.stdout)
                assert cli("workflow", "publish", "--file", str(candidate_file), stdout=subprocess.PIPE).returncode == 0
                assert terminal(start({}, workflow_id=induced_id)) == "completed"
                assert terminal(start(dict(file_path=str(second_file)), workflow_id=induced_id)) == "completed"
                assert not wire.call("workflow.run", workflow_id=induced_id, parameters=dict(file_path=1), policy="strict")["ok"]
                assert evidence.read_text() == "private experience fixture\n"
                review_file = root / "candidate.review.json"
                review_file.write_text(review_output.stdout)
                call("service.shutdown")
                service.wait(timeout=15)
                assert service.returncode == 0
                wire.sock.close()
                wire = None
                # A second process has no prior catalog, validation or Runs.
                # Only the explicit user-owned review file crosses the boundary.
                service = subprocess.Popen(service_command, env=env, stdout=log, stderr=log)
                deadline = time.monotonic() + 15
                while wire is None:
                    if service.poll() is not None:
                        raise RuntimeError("private service restart failed")
                    try:
                        wire = Wire(endpoint)
                    except OSError:
                        if time.monotonic() >= deadline:
                            raise TimeoutError("private service restart readiness")
                        time.sleep(0.02)
                assert call("workflow.list")["workflows"] == []
                assert call("workflow.runs")["runs"] == []
                damaged = root / "damaged.review.json"
                for field, value in (("digest", "0" * 64), ("workflow_id", uuid.uuid4().hex),
                                     ("definition", dict(parameterized, name="tampered")),
                                     ("definition", {})):
                    changed = json.loads(review_output.stdout)
                    changed["experience"][field] = value
                    damaged.write_text(json.dumps(changed))
                    rejected = cli("workflow", "import", "--file", str(damaged), stdout=subprocess.PIPE)
                    assert rejected.returncode != 0 and not rejected.stdout
                    assert str(evidence) not in rejected.stderr
                    assert call("workflow.list")["workflows"] == []
                    assert call("workflow.runs")["runs"] == []
                for content in ("{", "", compiled.stdout, " " * (384 * 1024 + 1)):
                    damaged.write_text(content)
                    rejected = cli("workflow", "import", "--file", str(damaged), stdout=subprocess.PIPE)
                    assert rejected.returncode != 0 and not rejected.stdout
                for command in (("workflow", "import"),
                                ("workflow", "import", "extra", "--file", str(review_file))):
                    assert cli(*command, stdout=subprocess.PIPE).returncode == 2
                imported = cli("workflow", "import", "--file", str(review_file), stdout=subprocess.PIPE)
                assert imported.returncode == 0
                assert f"workflow {induced_id}\n" in imported.stdout
                assert f"draft {review['digest']}\n" in imported.stdout
                assert str(evidence) not in imported.stderr
                assert call("workflow.get", workflow_id=induced_id)["definition"] == parameterized
                catalog = call("workflow.list")["workflows"]
                assert len(catalog) == 1 and catalog[0]["validation"] == "not_validated"
                assert catalog[0]["runnable"] is False
                assert not wire.call("workflow.run", workflow_id=induced_id, parameters={}, policy="strict")["ok"]
                assert call("workflow.runs")["runs"] == []
                duplicate = cli("workflow", "import", "--file", str(review_file), stdout=subprocess.PIPE)
                assert duplicate.returncode != 0 and not duplicate.stdout
                assert call("workflow.list")["workflows"] == catalog
                assert cli("workflow", "publish", "--file", str(candidate_file), stdout=subprocess.PIPE).returncode == 0
                assert terminal(start({}, workflow_id=induced_id)) == "completed"
                assert terminal(start(dict(file_path=str(second_file)), workflow_id=induced_id)) == "completed"
                # Duplicate import must not downgrade the newly published head.
                published_catalog = call("workflow.list")["workflows"]
                assert not wire.call("workflow.experience.import", archive=json.loads(review_output.stdout))["ok"]
                assert call("workflow.list")["workflows"] == published_catalog
                assert not wire.call("workflow.experience.compile", source_run_id=run_id,
                                     target_workflow_id=uuid.uuid4().hex, name="old run")["ok"]
                assert len(call("workflow.runs")["runs"]) == 2
                call("service.shutdown")
                service.wait(timeout=15)
                assert service.returncode == 0
                print("Workflow experience CLI: export/induction/review/bindings/no-mutation/rejection/output-error/publish/restart-import/draft-gate/content-identity passed")
            finally:
                if wire is not None:
                    wire.sock.close()
                if service.poll() is None:
                    service.terminate()
                    try:
                        service.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        service.kill()
                        service.wait(timeout=5)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--service", type=Path, required=True)
    run(parser.parse_args())
