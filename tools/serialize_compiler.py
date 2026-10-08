#!/usr/bin/env python3
"""Machine-wide compiler gate for memory-heavy Project Alice translation units."""

from __future__ import annotations

import fcntl
import os
import re
import resource
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple


GIB = 1024**3
LOCK_PATH = Path("/tmp/project-alice-compiler.lock")


def _command_output(args: List[str]) -> Optional[str]:
	try:
		return subprocess.check_output(args, text=True, stderr=subprocess.DEVNULL)
	except (OSError, subprocess.CalledProcessError):
		return None


def _linux_memory() -> Optional[Tuple[int, int]]:
	try:
		values: Dict[str, int] = {}
		for line in Path("/proc/meminfo").read_text().splitlines():
			match = re.match(r"^(MemTotal|MemAvailable):\s+(\d+)\s+kB$", line)
			if match:
				values[match.group(1)] = int(match.group(2)) * 1024
		return values["MemAvailable"], values["MemTotal"]
	except (OSError, KeyError):
		return None


def _mac_memory() -> Optional[Tuple[int, int]]:
	output = _command_output(["/usr/bin/vm_stat"])
	if not output:
		return None
	page_size_match = re.search(r"page size of (\d+) bytes", output)
	if not page_size_match:
		return None
	page_size = int(page_size_match.group(1))
	pages: Dict[str, int] = {}
	for line in output.splitlines():
		match = re.match(r"Pages ([^:]+):\s+([\d,]+)", line)
		if match:
			pages[match.group(1)] = int(match.group(2).replace(",", ""))
	try:
		available_pages = (
			pages["free"] + pages["inactive"] + pages["speculative"]
		)
		physical_pages = available_pages + pages["active"] + pages["throttled"] + pages["wired down"]
		physical_pages += pages.get("occupied by compressor", 0)
		return available_pages * page_size, physical_pages * page_size
	except KeyError:
		return None


def available_memory() -> Optional[Tuple[int, int]]:
	if sys.platform == "darwin":
		return _mac_memory()
	if sys.platform.startswith("linux"):
		return _linux_memory()
	return None


def minimum_free_bytes(total_bytes: int) -> int:
	configured = os.environ.get("ALICE_BUILD_MIN_FREE_GIB")
	if configured is not None:
		try:
			return max(0, int(float(configured) * GIB))
		except ValueError as error:
			raise SystemExit("ALICE_BUILD_MIN_FREE_GIB must be a non-negative number") from error
	# Leave at least a quarter of RAM free, with practical lower and upper bounds.
	return min(12 * GIB, max(4 * GIB, total_bytes // 4))


def wait_for_memory() -> bool:
	started = time.monotonic()
	try:
		wait_limit = max(0, int(os.environ.get("ALICE_BUILD_MEMORY_WAIT_SECONDS", "600")))
	except ValueError as error:
		raise SystemExit("ALICE_BUILD_MEMORY_WAIT_SECONDS must be an integer") from error
	last_notice = 0.0
	while True:
		memory = available_memory()
		if memory is None:
			print("Project Alice compiler gate: RAM availability could not be read; proceeding under the global lock.", file=sys.stderr)
			return True
		free_bytes, total_bytes = memory
		minimum = minimum_free_bytes(total_bytes)
		if free_bytes >= minimum:
			return True
		elapsed = time.monotonic() - started
		if elapsed >= wait_limit:
			print(
				"Project Alice compiler gate: not starting compiler; "
				f"available RAM is {free_bytes / GIB:.1f} GiB, "
				f"required is {minimum / GIB:.1f} GiB. "
				"Free memory or adjust ALICE_BUILD_MIN_FREE_GIB.",
				file=sys.stderr,
			)
			return False
		if elapsed - last_notice >= 30.0:
			print(
				"Project Alice compiler gate: waiting for memory; "
				f"available {free_bytes / GIB:.1f} GiB, "
				f"required {minimum / GIB:.1f} GiB.",
				file=sys.stderr,
				flush=True,
			)
			last_notice = elapsed
		time.sleep(5)


def acquire_global_lock(lock_fd: int) -> None:
	notice_printed = False
	while True:
		try:
			fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
			return
		except BlockingIOError:
			if not notice_printed:
				print(
					"Project Alice compiler gate: another checkout is compiling; "
					"waiting for the machine-wide lock.",
					file=sys.stderr,
					flush=True,
				)
				notice_printed = True
			time.sleep(5)


def main() -> int:
	command = sys.argv[1:]
	if not command:
		print("usage: serialize_compiler.py <compiler> [arguments...]", file=sys.stderr)
		return 2

	try:
		previous_umask = os.umask(0)
		try:
			lock_fd = os.open(LOCK_PATH, os.O_CREAT | os.O_RDWR, 0o666)
		finally:
			os.umask(previous_umask)
	except OSError as error:
		print(f"Project Alice compiler gate: cannot open {LOCK_PATH}: {error}", file=sys.stderr)
		return 2

	try:
		acquire_global_lock(lock_fd)
		if not wait_for_memory():
			return 75
		# Keep the lock alive in the compiler if CMake terminates this launcher.
		compile_started = time.monotonic()
		result = subprocess.run(command, pass_fds=(lock_fd,), check=False)
		if os.environ.get("ALICE_BUILD_REPORT_PEAK_RSS") == "1":
			peak = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
			if sys.platform != "darwin":
				peak *= 1024
			elapsed = time.monotonic() - compile_started
			print(
				f"Project Alice compiler gate: elapsed {elapsed:.1f}s, "
				f"compiler peak RSS {peak / GIB:.2f} GiB",
				file=sys.stderr,
			)
		return result.returncode
	finally:
		os.close(lock_fd)


if __name__ == "__main__":
	sys.exit(main())
