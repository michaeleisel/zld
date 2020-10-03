=============================
TBD Format Reference Manual
=============================

.. contents::
   :local:
   :depth: 4

.. _abstract:

Abstract
========
Text-based Dynamic Library Stubs (.tbd) are a new representation for dynamic
libraries and frameworks in Apple's SDKs. They provide the same benefit as the
legacy Mach-O Dynamic Library Stubs, which is to reduce the SDK size, but they
are even more compact. Furthermore, they enable several new features, such as
private framework inlining, InstallAPI, etc.

The new dynamic library stub file format is a human readable and editable YAML
text file. Those text-based stub files contain the same exported symbols as the
original dynamic library.


.. _format:

Format
======


.. _TBDv1:

TBD Version 1
-------------

The TBD v1 format only support two level address libraries and is per
definition application extension safe. It cannot represent flat namespace or
not application extension safe dynamic libraries.

The initial version of the TBD file doesn't have a YAML tag. The tag
*!tapi-tbd-v1* is optional and shouldn't be emitted to support older linker.

This is an example of an TBD version 1 file with all keys (optional and
required):

.. code::

  ---
  archs: [ armv7, armv7s, arm64 ]
  platform: ios
  install-name: /usr/lib/libfoo.dylib
  current-version: 1.2.3
  compatibility-version: 1.0
  swift-version: 1.0
  objc-constraint: none
  exports:
    - archs: [ armv7, armv7s, arm64 ]
      allowed-clients: [ clientA ]
      re-exports: [ /usr/lib/liba.dylib, /usr/lib/libb.dylib ]
      symbols: [ _sym1, _sym2 ]
      objc-classes: [ _NSString, _NSBlockPredicate ]
      objc-ivars: [ _NSBlockPredicate._block ]
      weak-def-symbols: [ _weakSym1, _weakSym2 ]
      thread-local-symbols: [ _tlvSym1, _tlvSym2 ]
    - archs: [ arm64 ]
      allowed-clients: [ clientB, clientC ]
      re-exports: [ /usr/lib/libc.dylib ]
      symbols: [ _sym3 ]
  ...

Keys:

- :ref:`archs <architectures>`
- :ref:`platform <platform>`
- :ref:`install-name <install_name>`
- :ref:`current-version <current_version>`
- :ref:`compatibility-version <compatibility_version>`
- :ref:`swift-version <swift_version>`
- :ref:`objc-constraint <objectivec_constraint>`
- :ref:`exports <exports>`


.. _TBDv2:

TBD Version 2
-------------

The TBD version 2 file uses the YAML tag *!tapi-tbd-v2*, which is required.
This version of the format adds new keys for UUIDs, flags, parent-umbrella, and
undefined symbols. This allows supporting flat namespace and non application
extension safe libraries. Furthermore, the *allowed-clients* key was renamed to
*allowable-clients* to be consistent with the static linker. The default value
for the key *objc-constraint* has been changed to *retain_release*.

This is an example of an TBD version 2 file with all keys (optional and
required):

.. code::

  --- !tapi-tbd-v2
  archs: [ armv7, armv7s, arm64 ]
  uuids: [ armv7: 00000000-0000-0000-0000-000000000000,
           armv7s: 11111111-1111-1111-1111-111111111111,
           arm64: 22222222-2222-2222-2222-222222222222 ]
  platform: ios
  flags: [ flat_namespace ]
  install-name: /usr/lib/libfoo.dylib
  current-version: 1.2.3
  compatibility-version: 1.0
  swift-version: 2
  objc-constraint: retain_release
  parent-umbrella: System
  exports:
    - archs: [ armv7, armv7s, arm64 ]
      allowable-clients: [ clientA ]
      re-exports: [ /usr/lib/liba.dylib, /usr/lib/libb.dylib ]
      symbols: [ _sym1, _sym2 ]
      objc-classes: [ _NSString, _NSBlockPredicate ]
      objc-ivars: [ _NSBlockPredicate._block ]
      weak-def-symbols: [ _weakSym1, _weakSym2 ]
      thread-local-symbols: [ _tlvSym1, _tlvSym2 ]
    - archs: [ arm64 ]
      allowable-clients: [ clientB, clientC ]
      re-exports: [ /usr/lib/libc.dylib ]
      symbols: [ _sym3 ]
  undefineds:
    - archs: [ armv7, armv7s, arm64 ]
      symbols: [ _sym10, _sym11 ]
      objc-classes: [ _ClassA ]
      objc-ivars: [ _ClassA.ivar1 ]
      weak-ref-symbols: [ _weakSym5 ]
    - archs: [ arm64 ]
      symbols: [ _sym12 ]
  ...

Keys:

- :ref:`archs <architectures>`
- :ref:`uuids <uuids>`
- :ref:`platform <platform>`
- :ref:`flags <flags>`
- :ref:`install-name <install_name>`
- :ref:`current-version <current_version>`
- :ref:`compatibility-version <compatibility_version>`
- :ref:`swift-version <swift_version>`
- :ref:`objc-constraint <objectivec_constraint>`
- :ref:`parent-umbrella <parent_umbrella>`
- :ref:`exports <exports>`
- :ref:`undefineds <undefineds>`


.. _TBDv3:

TBD Version 3
-------------

The TBD version 3 file uses the YAML tag *!tapi-tbd-v3*, which is required.
This version of the format adds new keys for Objective-C exception type and
renames the *swift-version* key to *swift-abi-version*, which also changes the
values that are encoded with this key. Furthermore, this version support
multiple YAML documents per TBD file, which is used by the private framework
inlining feature. The encoding of Objective-C class names and instance variables
has been changed to drop the leading '_'.

This is an example of an TBD version 3 file (without some optional keys):

.. code::

  --- !tapi-tbd-v3
  archs: [ armv7, armv7s, arm64 ]
  platform: ios
  install-name: /usr/lib/libfoo.dylib
  swift-abi-version: 3
  exports:
    - archs: [ armv7, armv7s, arm64 ]
      re-exports: [ /usr/lib/internal/liba.dylib ]
      symbols: [ _sym1, _sym2 ]
      objc-classes: [ NSString, NSBlockPredicate ]
      objc-eh-types: [ NSString ]
      objc-ivars: [ NSBlockPredicate._block ]
    - archs: [ arm64 ]
      symbols: [ _sym3 ]
  --- !tapi-tbd-v3
  archs: [ armv7, armv7s, arm64 ]
  platform: ios
  install-name: /usr/lib/liba.dylib
  swift-abi-version: 3
  exports:
    - archs: [ armv7, armv7s, arm64 ]
      re-exports: [ /usr/lib/internal/liba.dylib ]
      symbols: [ _sym10, _sym11 ]
  ...

Keys:

- :ref:`archs <architectures>`
- :ref:`uuids <uuids>`
- :ref:`platform <platform>`
- :ref:`flags <flags>`
- :ref:`install-name <install_name>`
- :ref:`current-version <current_version>`
- :ref:`compatibility-version <compatibility_version>`
- :ref:`swift-abi-version <swift_abi_version>`
- :ref:`objc-constraint <objectivec_constraint>`
- :ref:`parent-umbrella <parent_umbrella>`
- :ref:`exports <exports>`
- :ref:`undefineds <undefineds>`


.. _TBDv4:

TBD Version 4
-------------

The TBD version 4 file uses the YAML tag *!tapi-tbd*, which is required. This
version of the format has several fundamental changes from the previous
formats. This change was necessary to support new features and platforms.

This is an example of an TBD version 4 file:

.. code::

  --- !tapi-tbd
  tbd-version: 4
  targets: [ i386-macos, x86_64-macos, x86_64-<6>]
  uuids:
    - target: i386-macos
      value: xxx
    - target: x86_64-macos
      value: xxx
    - target: x86_64-<6>
      value: xxx
  flags: []
  install-name: /u/l/libfoo.dylib
  current-version: 1.2.3
  compatibility-version: 1.1
  swift-abi-version: 5
  parent-umbrella:
    - targets: [ i386-macos, x86_64-macos, x86_64-<6>]
      umbrella: System
  allowable-clients:
    - targets: [ i386-macos, x86_64-macos, x86_64-<6>]
      clients: [ ClientA, ClientB ]
  reexported-libraries:
    - targets: [ x86_64-macos, x86_64-<6>]
      library: [ /System/Library/Frameworks/Foo.framework/Foo ]
    - targets: [ i386-macos]
      library: [ /System/Library/Frameworks/Bar.framework/Bar ]
  exports:
    - targets: [ x86_64-macos ]
      symbols: [ _symA ]
      objc-classes: []
      objc-eh-types: []
      objc-ivars: []
      weak-symbols: []
      thread-local-symbols: []
    - targets: [ x86_64-<6> ]
      symbols: [ _symB ]
    - targets: [ x86_64-macos, x86_64-<6> ]
      symbols: [ _symAB ]
  re-exports:
    - targets: [ i386-macos ]
      symbols: [ _symC ]
      objc-classes: []
      objc-eh-types: []
      objc-ivars: []
      weak-symbols: []
      thread-local-symbols: []
  undefineds:
    - targets: [ i386-macos ]
      symbols: [ _symD ]
      objc-classes: []
      objc-eh-types: []
      objc-ivars: []
      weak-symbols: []
      thread-local-symbols: []
  ...

Keys:

- :ref:`tbd-version <tbd_version>`
- :ref:`targets <targets>`
- :ref:`uuids <uuids_v4>`
- :ref:`flags <flags>`
- :ref:`install-name <install_name>`
- :ref:`current-version <current_version>`
- :ref:`compatibility-version <compatibility_version>`
- :ref:`swift-abi-version <swift_abi_version>`
- :ref:`parent-umbrella <parent_umbrella_v4>`
- :ref:`allowable-clients <allowable_clients_v4>`
- :ref:`reexported-libraries <reexported_libraries_v4>`
- :ref:`exports <exports_v4>`
- :ref:`undefineds <undefineds_v4>`


.. _tbd_version:

TBD File Version
~~~~~~~~~~~~~~~~

The key *tbd-version* is required and specifies the TBD file version.

Example:

.. code::

  tbd-version: 4

Currently the only valid value is 4.


.. _targets:

Targets
~~~~~~~

The key *targets* is required and specifies a list of supported
architecture/platform tuples.

Example:

.. code::

  targets: [ x86_64-macos, x86_64-<6>, arm64-ios, x86_64-ios-simulator ]


.. _parent_umbrella_v4:

Parent Umbrella
~~~~~~~~~~~~~~~

The key *parent-umbrella* is optional and specifies the parent umbrella of the
dynamic library (if applicable).

Example:

.. code::

  parent-umbrella:
    - targets: [ arm64-ios ]
      umbrella: System
    - targets: [ x86_64-ios-simulator]
      umbrella: SystemSim


.. _allowable_clients_v4:

Allowable Clients
~~~~~~~~~~~~~~~~~

The key *allowable-clients* is optional and specifies a list of allowable
clients that are permitted to link against the dynamic library file.

Example:

.. code::

  allowable-clients:
    - targets: [ arm64-ios ]
      clients: [ ClientA, ClientB ]
    - targets: [ x86_64-ios-simulator ]
      clients: [ ClientC ]


.. _reexported_libraries_v4:

Reexported Libraries
~~~~~~~~~~~~~~~~~~~~

The key *reexported-libraries* is optional and specifies a list of reexported
libraries.

Example:

.. code::

  reexported-libraries:
    - targets:   [ arm64-ios ]
      libraries: [ /usr/lib/libm.dylib ]
    - targets:   [ x86_64-ios-simulator ]
      libraries: [ /usr/lib/libobjc4.dylib ]

.. _common:

Common Keys
--------------------

.. _architectures:

Architectures
~~~~~~~~~~~~~

The key *archs* is required and specifies the list of architectures that are
supported by the dynamic library file.

Example:

.. code::

  archs: [ armv7, armv7s, arm64 ]

Valid architectures are: i386, x86_64, x86_64h, armv7, armv7s, armv7k, arm64


.. _uuids:

UUIDs
~~~~~

The key *uuids* is optional and specifies the list of UUIDs per architecture.

Example:

.. code::

  uuids: [ armv7: 00000000-0000-0000-0000-000000000000,
           armv7s: 11111111-1111-1111-1111-111111111111,
           arm64: 22222222-2222-2222-2222-222222222222 ]


.. _platform:

Platform
~~~~~~~~

The key *platform* is required and specifies the platform that is supported by
the dynamic library file.

Example:

.. code::

  platform: macosx

Valid platforms are: macosx, ios, tvos, watchos


.. _flags:

Flags
~~~~~

The key *flags* is optional and specifies dynamic library specific flags.

Example:

.. code::

  flags: [ installapi ]

Valid flags are: flat_namespace, not_app_extension_safe, and installapi.


.. _install_name:

Install Name
~~~~~~~~~~~~

The key *install-name* is required and specifies the install name of the dynamic
library file, which is usually the path in the SDK.

Example:

.. code::

  install-name: /System/Library/Frameworks/Foundation.framework/Foundation


.. _current_version:

Current Version
~~~~~~~~~~~~~~~

The key *current-version* is optional and specifies the current version of the
dynamic library file. The default value is 1.0 if not specified.

Example:

.. code::

  current-version: 1.2.3


.. _compatibility_version:

Compatibility Version
~~~~~~~~~~~~~~~~~~~~~

The key *compatibility-version* is optional and specifies the compatibility
version of the dynamic library file. The default value is 1.0 if not specified.

Example:

.. code::

  compatibility-version: 1.2


.. _swift_version:

Swift Version
~~~~~~~~~~~~~

The key *swift-version* is optional and specifies the Swift version the
dynamic library file was compiled with. The default value is 0 if not
specified.

Example:

.. code::

  swift-version: 1.0


.. _swift_abi_version:

Swift ABI Version
~~~~~~~~~~~~~~~~~

The key *swift-abi-version* is optional and specifies the Swift ABI version the
dynamic library file was compiled with. The default value is 0 if not
specified.

Example:

.. code::

  swift-abi-version: 5


.. _objectivec_constraint:

Objective-C Constraint
~~~~~~~~~~~~~~~~~~~~~~

The key *objc-constraint* is optional and specifies the Objective-C constraint
that was used to compile the dynamic library file. The default value is *none*
for TBD v1 files and *retain_release* thereafter.

Example:

.. code::

  objc-constraint: retain_release

Valid Objective-C constraints are: none, retain_release,
retain_release_for_simulator, retain_release_or_gc, or gc.


.. _parent_umbrella:

Parent Umbrella
~~~~~~~~~~~~~~~

The key *parent-umbrella* is optional and specifies the parent umbrella of the
dynamic library (if applicable).

Example:

.. code::

  parent-umbrella: System


.. _exports:

Export Section
~~~~~~~~~~~~~~

The key *exports* is optional, but it is very uncommon to have a dynamic
library that does not export any symbols. Symbol names, Objective-C Class
names, etc, are grouped into sections. Each section defines a unique
architecture set. This is an optimization to reduce the size of the file, by
grouping common symbol names into the same section.

Example (TBD v1):

.. code::

  exports:
    - archs: [ armv7, armv7s, arm64 ]
      allowed-clients: [ clientA ]
      re-exports: [ /usr/lib/liba.dylib, /usr/lib/libb.dylib ]
      symbols: [ _sym1, _sym2 ]
      objc-classes: [ _NSString, _NSBlockPredicate ]
      objc-ivars: [ _NSBlockPredicate._block ]
      weak-def-symbols: [ _weakSym1, _weakSym2 ]
      thread-local-symbols: [ _tlvSym1, _tlvSym2 ]


Example (TBD v2):

.. code::

  exports:
    - archs: [ armv7, armv7s, arm64 ]
      allowable-clients: [ clientA ]
      re-exports: [ /usr/lib/liba.dylib, /usr/lib/libb.dylib ]
      symbols: [ _sym1, _sym2 ]
      objc-classes: [ _NSString, _NSBlockPredicate ]
      objc-ivars: [ _NSBlockPredicate._block ]
      weak-def-symbols: [ _weakSym1, _weakSym2 ]
      thread-local-symbols: [ _tlvSym1, _tlvSym2 ]
  

Example (TBD v3):

.. code::

  exports:
    - archs: [ armv7, armv7s, arm64 ]
      allowable-clients: [ clientA ]
      re-exports: [ /usr/lib/liba.dylib, /usr/lib/libb.dylib ]
      symbols: [ _sym1, _sym2 ]
      objc-classes: [ NSString, NSBlockPredicate ]
      objc-eh-types: [ NSString ]
      objc-ivars: [ NSBlockPredicate._block ]
      weak-def-symbols: [ _weakSym1, _weakSym2 ]
      thread-local-symbols: [ _tlvSym1, _tlvSym2 ]


Each section has the following keys:
  - :ref:`archs <architectures>`
  - :ref:`allowed-clients <allowable_clients>` (TBD v1) or
    :ref:`allowable-clients <allowable_clients>` (TBD v2 and TBD v3)
  - :ref:`re-exports <reexported_libraries>`
  - :ref:`symbols <symbols>`
  - :ref:`objc-classes <objectivec_classes>`
  - :ref:`objc-eh-types <objectivec_eh_types>` (TBD v3 only)
  - :ref:`objc-ivars <objectivec_ivars>`
  - :ref:`weak-def-symbols <weak_symbols>`
  - :ref:`thread-local-symbols <thread_local_symbols>`


.. _undefineds:

Undefined Section
~~~~~~~~~~~~~~~~~

The key *undefineds* is optional and only applies to flat namespace libraries.

Example (TBD v2):

.. code::

  undefineds:
    - archs: [ armv7, armv7s, arm64 ]
      symbols: [ _sym1, _sym2 ]
      objc-classes: [ _NSString, _NSBlockPredicate ]
      objc-ivars: [ _NSBlockPredicate._block ]
      weak-ref-symbols: [ _weakSym1, _weakSym2 ]
  
Example (TBD v3):

.. code::

  undefineds:
    - archs: [ armv7, armv7s, arm64 ]
      symbols: [ _sym1, _sym2 ]
      objc-classes: [ NSString, NSBlockPredicate ]
      objc-eh-types: [ NSString ]
      objc-ivars: [ _NSBlockPredicate._block ]
      weak-ref-symbols: [ _weakSym1, _weakSym2 ]


Each section has the following keys:
  - :ref:`archs <architectures>`
  - :ref:`symbols <symbols>`
  - :ref:`objc-classes <objectivec_classes>`
  - :ref:`objc-eh-types <objectivec_eh_types>` (TBD v3 only)
  - :ref:`objc-ivars <objectivec_ivars>`
  - :ref:`weak-ref-symbols <weak_symbols>`


  .. _allowable_clients:

Allowable Clients
~~~~~~~~~~~~~~~~~

The key *allowed-clients* in TBD format 1 or *allowable-clients* in the TBD
format 2 and later is optional and specifies a list of allowable clients that
are permitted to link against the dynamic library file.

Example (TBD v1):

.. code::

  allowed-clients: [ clientA ]

Example (TBD v2 + v3):

.. code::

  allowable-clients: [ clientA ]


.. _reexported_libraries:

Reexported Libraries
~~~~~~~~~~~~~~~~~~~~

The key *re-exports* is optional and specifies a list of re-exported library
names.

Example:

.. code::

  re-export: [ /usr/lib/libm.dylib ]


.. _symbols:

Symbol Names:
~~~~~~~~~~~~~

The key *symbols* is optional and specifies a list of exported or undefined
symbol names.

Example:

.. code::

  symbols: [ _sym1, _sym2, _sym3 ]


.. _objectivec_classes:

Objective-C Class Names:
~~~~~~~~~~~~~~~~~~~~~~~~

The key *objc-classes* is optional and specifies a list of exported or undefined
Objective-C class names. Objective-C classes have different symbol mangling
depending on the Objective-C ABI, which would prevent the merging of
Objective-C class symbols across architecture slices. Therefore they are listed
separately from other symbols, which avoids the mangling issue and allows the
merging across architecture slices.


Example (TBD v1 and TBD v2):

.. code::

  objc-classes: [ _ClassA, _ClassB, _ClassC ]

Example (TBD v3):

.. code::

  objc-classes: [ ClassA, ClassB, ClassC ]


.. _objectivec_eh_types:

Objective-C Exception Type:
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The key *objc-eh-types* is optional and specifies a list of exported or
undefined Objective-C class exception types.

Example (TBD v3):

.. code::

  objc-eh-types: [ ClassA, ClassB ]


.. _objectivec_ivars:

Objective-C Instance Variables:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The key *objc-ivars* is optional and specifies a list of exported or undefined
Objective-C instance variable names.

Example (TBD v1 and TBD v2):

.. code::

  objc-ivars: [ _ClassA.ivar1, _ClassA.ivar2, _ClassC.ivar1 ]

Example (TBD v3):

.. code::

  objc-ivars: [ ClassA.ivar1, ClassA.ivar2, ClassC.ivar1 ]


.. _weak_symbols:

Weak Symbols:
~~~~~~~~~~~~~~~~~~~~~

The key *weak-def-symbols* for export sections or *weak-ref-symbols* for
undefined sections is optional and specifies a list of weak symbol names.

Example (Export Section):

.. code::

  weak-def-symbols: [ _weakDef1, _weakDef2 ]


Example (Undefined Section):

.. code::

  weak-ref-symbols: [ _weakRef1, _weakRef2 ]


.. _thread_local_symbols:

Thread Local Symbols:
~~~~~~~~~~~~~~~~~~~~~

The key *thread-local-symbols* is optional and specifies a list of thread local
exported symbol names.

Example:

.. code::

  thread-local-symbols: [ _tlv1, _tlv2 ]


