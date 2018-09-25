Import('RTT_ROOT')
Import('rtconfig')
from building import *

cwd = GetCurrentDir()
src = ['src/rdbd.c', 'src/rdbd_service.c', 'src/rdbd_service_manager.c', 'src/rdbd_service_base.c']

if GetDepend(['PKGS_USING_USB_RDBD']):
    src += ['src/usb_rdbd.c']

if GetDepend(['PKGS_USING_RDBD_FILE']):
    src += ['examples/rdbd_file.c']

if GetDepend(['PKGS_USING_RDBD_SHELL']):
    src += ['examples/rdbd_shell.c']

CPPPATH = [cwd + '/inc']

group = DefineGroup('rdb', src, depend = ['PKG_USING_RDB'], CPPPATH = CPPPATH)

Return('group')
