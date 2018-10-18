# README #

**Hammertime**: a software suite for testing, profiling and simulating the Rowhammer DRAM defect, built on top of the [RAMSES address translation library](https://github.com/vusec/ramses).

### What does this project contain? ###

Hammertime contains two major components:

* `profile`: a tool to test and collect data about a running system's vulnerability to Rowhammer. For more information, check out its own README file under `profile/README`.
* A Rowhammer exploit simulator, useful for rapid evaluation of exploit effectiveness
* Various small tools and utilities:
	* `py/hammertime` -- Python interface to working with `profile` output files
	* `py/hammerstats.py` prints basic statistics about the output of a `profile` run. Demo usage of the Python interface.
	* `py/prettyprofile.py` converts a `profile` output into something more human-friendly.
	* `py/common_flips.py` processes multiple `profile` results selecting only bit flips common to all. Useful for finding bit flips that can be reliably triggered.

For an in-depth view of the overall architecture of Hammertime check out the paper "Defeating Software Mitigations against Rowhammer: A Surgical Precision Hammer" by Tatar et al. presented at RAID'18.

### How do I get set up? ###

#### Dependencies ####

* POSIX-compatible OS (Linux recommended)
* Python >= 3.2 --- used by tools
* RAMSES (included as [git submodule](https://git-scm.com/book/en/v2/Git-Tools-Submodules); make sure to clone recursively or manually initi and update before building)

#### Building ####

Run `make` in the root directory to build all Hammertime components and tools.

`make clean` removes all previously built files.

### Getting started ###

#### Detecting your system's memory configuration ####

A memory configuration (i.e. `.msys`) file includes information about the memory controller, physical address routing, DRAM geometry and optional on-chip remapping.
Figuring these out by hand is tedious; here's where a tool comes in.

Run `ramses/tools/msys_detect.py`, ideally as a superuser.
It will try to auto-detect most parameters and ask you for the others.

The output file it produces can now be used by other Hammertime components.

#### Testing for Rowhammer ####

The profile tool works best with elevated permissions.
We recommend running it as root or running `make cap` as root in its directory to set the necessary capabilities on the binary.

Example run with only the essential arguments:

`profile/profile 256m spam.msys`

will run a basic double-sided rowhammer attack over 256MiB of RAM using `spam.msys` as memory configuration file.

The output may seem a bit cryptic. To remedy this, use the prettifying script:

`profile/profile 256m spam.msys | py/prettyprofile.py -`

as a shell pipeline or

```
tools/profile/profile -s 256m spam.msys myprof.res
py/prettyprofile.py myprof.res
```

by using a temporary file.

Check out `profile`'s own README file for (many) more command line options and the format of its raw output.

#### Simulating bit flips ####

The `hammertime.sim` Python package provides an API for evaluating the potential effectiveness of Rowhammer exploits, using memory profiles output by Hammertime `profile`.
Several examples of such exploits are provided:

* `py/dem_exploit.py` -- Dedup Est Machina (S&P'16)
* `py/ffs_exploit.py` -- Flip Fen Shui (Black Hat Europe '16)
* `py/x86pte_exploits.py` -- Exploits targeting parts of an x86(_64) page table entry (PTE)

We also provide a repository with `profile` outputs captured on vulnerable hardware available [here](https://github.com/vusec/hammertime-fliptables).

### How can I contribute? ###

#### I found a bug! ####

Report it on the bug tracker [here](https://github.com/andreittr/hammertime/issues).
