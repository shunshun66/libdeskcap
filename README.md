Libdeskcap
==========

Libdeskcap is a high-performance desktop and window capture library with a simplified interface written in C++. This library is a part of the [Mishira project](http://www.mishira.com).

License
=======

Unless otherwise specified all parts of Libdeskcap are licensed under the standard GNU General Public License v2 or later versions. A copy of this license is available in the file `LICENSE.GPL.txt`.

Any questions regarding the licensing of Libdeskcap can be sent to the authors via their provided contact details.

Version history
===============

A consolidated version history of Libdeskcap can be found in the `CHANGELOG.md` file.

Stability
=========

Libdeskcap's API is currently extremely unstable and can change at any time. The Libdeskcap developers will attempt to increment the version number whenever the API changes but users of the library should be warned that every commit to Libdeskcap could potentially change the API, ABI and overall behaviour of the library.

Building
========

Building Libdeskcap is nearly identical to building the main Mishira application. Detailed instructions for building Mishira can be found in the main Mishira Git repository. Right now development builds of Libdeskcap are compiled entirely within the main Visual Studio solution which is the `Libdeskcap.sln` file in the root of the repository. Please do not upgrade the solution or project files to later Visual Studio versions if asked.

Libdeskcap depends on Libvidgfx (Another Mishira library), Qt, Google Test, Boost and GLEW. Instructions for building these dependencies can also be found in the main Mishira Git repository.

Usage
=====

There are several dangers that users of the library should be wary about.

The first danger is that Libdeskcap relies on interprocess communication using shared memory. As the shared memory segment names are hard-coded into Libdeskcap if two instances of Libdeskcap are executing on the system at once they will interfere with each other and almost certainly stop working correctly (Maybe even crash!). Users of the library should do everything in their power to prevent multiple instances of their application from being executed at once.

The second danger is that since Libdeskcap injects code into other processes that are running on the system in order to capture window and screen back buffers that are not normally exposed, it can potentially conflict with other applications that do the exact same thing such as other video game broadcasters, recorders, screen overlays, debug utilities, and even Steam and Origin. It is entirely possible that the process that is having code injected into could crash as a result of these conflicts.

The third danger is that virus scanners hate anything that injects code into other processes. Virus scanners may prevent injection from ever happening or even quarantine or outright delete Libdeskcap's DLLs and helpers without warning. There's not much we can do in regards to this other than informing end users of the software and attempting to obfuscate any strings that virus scanners may search for.

Contributing
============

Want to help out on Libdeskcap? We don't stop you! New contributors to the project are always welcome even if it's only a small bug fix or even if it's just helping spread the word of the project to others. You don't even need to ask; just do it!

More detailed guidelines on how to contribute to the project can be found in the main Mishira Git repository.
