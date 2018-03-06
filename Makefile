PROGRAM = gitbslr
ARTYPE = dll
ARGUI = 0
AROPENGL = 0
ARTHREAD = 0
ARWUTF = 0
ARSOCKET = 0
#valid values: openssl (default), gnutls, tlse, bearssl, no
ARSOCKET_SSL = openssl
#valid values: schannel (default), bearssl, no (others may work, not tested)
ARSOCKET_SSL_WINDOWS = schannel
ARSANDBOX = 0

include arlib/Makefile
