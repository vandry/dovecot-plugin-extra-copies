dovecot-plugin-extra-copies
===========================

:Author: Kim Vandry <vandry@TZoNE.ORG>

Introduction
~~~~~~~~~~~~

Provides a Dovecot plugin called "extra_copies" which allows extra
copies of messages to be made in other folders whenever a message is
added to (copied, appended, posted with LDA) to a particular folder.

Configuration
~~~~~~~~~~~~~

Activate the plugin as usual by listing is in mail_plugins:

mail_plugins = (other plugins) ... extra_copies

Then create a configuration file called "extra-copies" inside each
mailbox for which extra copies of messages are desired. List one or
more OTHER mailbox names, one per line, in this file.

Whenever a message is added to the folder that contains the
"extra-copies" control file, additional copies of the message will
be made in each of the folders listed in the "extra-copies" file.

Be careful to never create an infinite loop (messages in "a" get
extra copies in "b" and messages in "b" get extra copies in "a")!
