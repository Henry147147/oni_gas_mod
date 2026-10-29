#!/usr/bin/env python3
"""Require the existing smoke fixture to run the current activity owner.

This checks dispatch/custody provenance, not performance acceptance. CMake
supplies the unchanged executable/--preset smoke command, prefixed by its
cross-compiling emulator where applicable.
"""
from __future__ import annotations
import json
import subprocess
import sys


def verify(result: dict) -> None:
    if not isinstance(result, dict) or result.get('profile') != 9 or result.get('owner_flags') != 29:
        raise ValueError('The smoke fixture must run current profile9 / owner flags29')
    if result.get('activity_tiers_enabled') is not True or result.get('fully_active_stress') is not False:
        raise ValueError('The smoke fixture must use the current activity-tier policy')
    if result.get('frames') != 3 or result.get('census_committed') != 3:
        raise ValueError('All three smoke frames must commit their own activity census')
    if type(result.get('native_substeps')) is not int or result['native_substeps'] <= 0:
        raise ValueError('The smoke fixture did not evolve the native owner')
    observation = result.get('activity_observation')
    if not isinstance(observation, dict) or type(observation.get('actor_cells')) is not int or observation['actor_cells'] <= 0:
        raise ValueError('The smoke fixture did not publish the actual actor census')
    if result.get('faulted') is not False:
        raise ValueError('The smoke owner faulted')


def main(arguments: list[str]) -> int:
    if not arguments or arguments[0] != '--' or len(arguments) < 2:
        print('usage: check_colony_benchmark.py -- EXECUTABLE [EMULATOR/SMOKE ARGUMENTS]', file=sys.stderr)
        return 2
    result = subprocess.run(arguments[1:], capture_output=True, text=True)
    if result.stdout:
        print(result.stdout, end='')
    if result.stderr:
        print(result.stderr, end='', file=sys.stderr)
    if result.returncode:
        return result.returncode
    try:
        verify(json.loads(result.stdout))
    except (ValueError, TypeError) as error:
        print('colony benchmark smoke provenance: ' + str(error), file=sys.stderr)
        return 1
    print('PASS current-profile colony benchmark:9/flags29, three committed censuses, evolved owner and live actor policy')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
