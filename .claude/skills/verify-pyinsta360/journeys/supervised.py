"""Run discover+open+battery under run_supervised N times; count SDK discovery crashes retried."""
import argparse
import logging

import pyinsta360 as pi
from _common import check, finish, write_json


def probe():
    pi.set_log_level(pi.LogLevel.ERR)
    devices = pi.discover()
    with pi.Camera() as cam:
        battery = cam.battery_status()
        return {"devices": len(devices), "serial": cam.info.serial_number,
                "battery": battery.battery_level}


class _RetryCounter(logging.Handler):
    def __init__(self):
        super().__init__(logging.WARNING)
        self.messages = []

    def emit(self, record):
        self.messages.append(record.getMessage())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iterations", type=int, default=10)
    ap.add_argument("--retries", type=int, default=3)
    args = ap.parse_args()

    counter = _RetryCounter()
    logging.getLogger("pyinsta360").addHandler(counter)
    runs = []
    for i in range(args.iterations):
        before = len(counter.messages)
        try:
            result = pi.run_supervised(probe, retries=args.retries)
            error = None
        except pi.SupervisedProcessError as exc:
            result, error = None, f"{exc} (attempts={exc.attempts}, exitcode={exc.exitcode}, during_discovery={exc.during_discovery})"
        retried = len(counter.messages) - before
        runs.append({"iteration": i, "retries": retried, "result": result, "error": error})
        print(f"iteration {i}: retries={retried} result={result} error={error}", flush=True)

    total_retries = sum(r["retries"] for r in runs)
    summary = {"iterations": args.iterations, "succeeded": sum(r["error"] is None for r in runs),
               "total_retries": total_retries, "warnings": counter.messages, "runs": runs}
    write_json("supervised_summary.json", summary)
    print("summary:", {k: v for k, v in summary.items() if k not in ("runs", "warnings")})
    check(summary["succeeded"] == args.iterations, "every supervised run returned a result")
    check(all(r["result"] and r["result"]["devices"] >= 1 for r in runs if r["result"]), "each result saw the camera")
    print(f"NOTE {total_retries} SDK discovery crash(es) were caught and retried")
    finish()


if __name__ == "__main__":
    main()
