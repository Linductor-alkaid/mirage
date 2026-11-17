"""DEC-148: bounded Unix peer exercises the real CLI's wire/error contract.

The real RuntimeService/atom/model integration is tested separately in C++.
This peer never dials a model or uses credentials, display or user settings.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile


def read_exact(peer, size):
    data = b""
    while len(data) < size:
        part = peer.recv(size - len(data))
        if not part:
            raise RuntimeError("CLI closed IPC")
        data += part
    return data


def run(cli):
    run_id = "1" * 32
    metadata = dict(run_id=run_id, action="status", settled=False, ordinal=0,
                    workflow_id="", outcome="", reason_code="", decision_digest="",
                    patch_id="", model_request_id="", lesson_id="", lesson_digest="")
    with tempfile.TemporaryDirectory(prefix="mirage-recovery-cli-") as temp:
        root = Path(temp)
        endpoint = root / "peer.sock"

        def invoke(action="status", patch=None, payload=None, failure=False, stdout=subprocess.PIPE):
            endpoint.unlink(missing_ok=True)
            with socket.socket(socket.AF_UNIX) as server:
                server.settimeout(10)
                server.bind(str(endpoint))
                os.chmod(endpoint, 0o600)
                server.listen(1)
                process = subprocess.Popen([str(cli), "workflow", "recovery", action, run_id,
                                            "--socket", str(endpoint)],
                                           stdout=stdout, stderr=subprocess.PIPE, text=True)
                try:
                    peer, _ = server.accept()
                    with peer:
                        peer.settimeout(10)
                        size = struct.unpack("<I", read_exact(peer, 4))[0]
                        assert size <= 1048576
                        request = json.loads(read_exact(peer, size))
                        assert request["op"] == "workflow.recovery." + action
                        assert request["run_id"] == run_id
                        response = dict(v=1, id=request["id"], ok=not failure)
                        if failure:
                            response["error"] = dict(code="unsupported", message="old service")
                        elif payload:
                            response.update(payload)
                        else:
                            response["recovery"] = dict(metadata, action=action, **(patch or {}))
                        data = json.dumps(response, separators=(",", ":")).encode()
                        peer.sendall(struct.pack("<I", len(data)) + data)
                    output, error = process.communicate(timeout=10)
                    return process.returncode, output, error
                finally:
                    if process.poll() is None:
                        process.kill()
                    process.wait(timeout=10)

        for action in ("start", "status", "cancel", "lesson"):
            fields = dict(lesson_id="fixture-lesson", lesson_digest="a" * 64) if action == "lesson" else {}
            code, output, error = invoke(action, patch=fields)
            assert code == 0 and not error, (code, error)
            value = json.loads(output)["recovery"]
            assert value == dict(metadata, action=action, **fields)
        code, output, _ = invoke(patch=dict(settled=True, ordinal=1, workflow_id="2" * 32,
                                          outcome="patched-and-resumed", patch_id="3" * 32))
        assert code == 0 and json.loads(output)["recovery"]["settled"] is True
        for patch in (dict(run_id="2" * 32), dict(action="cancel"), dict(settled="true"),
                      dict(ordinal=2), dict(outcome="completed")):
            # action is intentionally replaced for the mismatched-response case.
            if "action" in patch:
                code, output, _ = invoke(payload=dict(recovery=dict(metadata, **patch)))
            else:
                code, output, _ = invoke(patch=patch)
            assert code != 0 and not output
        assert invoke(failure=True)[0] != 0
        assert invoke(payload=dict(learning=dict(enabled=False, file_bytes=0, limit_bytes=0,
                                                reserve_bytes=0, peak_admitted_file_bytes=0)))[0] != 0
        if Path("/dev/full").exists():
            with open("/dev/full", "w") as full:
                assert invoke(stdout=full)[0] != 0
        for operands in ([], ["retry", run_id], ["start"], ["start", "bad"],
                         ["start", "0" * 32], ["status", run_id, "extra"]):
            result = subprocess.run([str(cli), "workflow", "recovery", *operands],
                                    capture_output=True, text=True, timeout=10)
            assert result.returncode == 2 and not result.stdout
    print("workflow_recovery_cli_test: passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", type=Path, required=True)
    run(parser.parse_args().cli)
