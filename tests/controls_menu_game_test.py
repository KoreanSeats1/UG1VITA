"""Check the original game's menu bar recognizes the footer and cannot activate its former Quit action.

Uses the user's own Speed.exe and Unicorn, like camera_game_test.py; no game assets are redistributed.
"""
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / '.pydeps'), str(ROOT / 'tools/recompiler'),
                str(ROOT / 'tools/recompiler/disasm')]
from disasm.image import Image  # noqa: E402
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX,
                              UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_EIP)  # noqa: E402


def check(path):
    image = Image(str(path))
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(0x400000, 0x800000)
    for section in image.sections:
        uc.mem_write(section.address, section.data)
    stack, stop = 0xb00000, 0xb01000
    controller, vtable, record, package = 0xa00000, 0xa00100, 0xa00200, 0xa00300
    bar, quit_button, back_button, default_button = 0xa01000, 0xa02000, 0xa02100, 0xa02200

    def put(a, v):
        uc.mem_write(a, struct.pack('<I', v))

    def get(a):
        return struct.unpack('<I', uc.mem_read(a, 4))[0]

    def return_from_call(arg_bytes=0, eax=0):
        esp = uc.reg_read(UC_X86_REG_ESP)
        uc.reg_write(UC_X86_REG_EIP, get(esp))
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + arg_bytes)
        uc.reg_write(UC_X86_REG_EAX, eax)

    calls = []
    tags = {0x8bdc3c7d, 0xa1d233eb, 0x677392c4}  # Controls asset's Quit, Back, Default tags

    def hook(_uc, address, _size, _user):
        if address == 0x4f3f90:  # package-resource lookup (one argument)
            return_from_call(4, record)
        elif address == 0x4ffb70:  # look up a tag in this screen's package
            return_from_call(eax=0xa03000 if uc.reg_read(UC_X86_REG_ECX) in tags else 0)
        elif address == 0x4f7eb0:  # mouse hit test; disabled Quit must never reach it
            obj = uc.reg_read(UC_X86_REG_ECX)
            assert obj != quit_button, 'old Quit mouse activation must be disabled'
            calls.append(('hit', obj))
            return_from_call(4)
        elif address == 0x4e6ed0:  # no modal dialog prevents Default
            return_from_call(4)
        elif address == 0x4e77c0:  # dispatch to the current screen
            calls.append(('event', get(uc.reg_read(UC_X86_REG_ESP) + 8)))
            return_from_call(8)
        elif address == 0x4c96c0:  # dispatch by the current screen's package name
            calls.append(('event', get(uc.reg_read(UC_X86_REG_ESP) + 4)))
            return_from_call()
        elif address == 0x4f6630:
            raise AssertionError('the former Quit action opened a dialog')

    uc.hook_add(UC_HOOK_CODE, hook)
    put(controller, vtable)
    put(controller + 0xc, 0x6c20e4)  # PC_joypad_config.fng
    put(vtable + 4, 0x4134b0)  # real Controls footer-mask filter
    put(record + 0x18, package)

    def mask(quit_tag):
        put(0x706fd8, quit_tag)
        uc.mem_write(stack, struct.pack('<2I', stop, controller))
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.emu_start(0x4f8000, stop, count=10000)
        return get(0x735780)

    assert mask(0) == (1 << 4) | (1 << 15)
    assert mask(0x8bdc3c7d) == 1 | (1 << 4) | (1 << 15)
    put(bar + 0x40, 3)
    for i, (obj, name) in enumerate([(quit_button, 0x46fce6dd), (back_button, 0x1c7fcf8b),
                                     (default_button, 0x608b3c7f)]):
        put(bar + 0x44 + i * 4, obj)
        put(obj + 0x10, name)
    uc.mem_write(bar + 0x144, b'\x00\x01\x01')  # finishMenuBar suppresses the former Quit activation

    def key(c):
        uc.mem_write(0x7363b1, c.encode())
        uc.mem_write(stack, struct.pack('<2I', stop, bar))
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.emu_start(0x4e6f00, stop, count=10000)

    key('q')
    assert calls == []
    uc.mem_write(stack, struct.pack('<I', stop))
    uc.reg_write(UC_X86_REG_ESP, stack)
    uc.reg_write(UC_X86_REG_ESI, bar)
    uc.emu_start(0x4e7320, stop, count=10000)
    assert calls == [('hit', back_button), ('hit', default_button)]
    calls.clear()
    key('z')
    assert calls, 'Restore Defaults must still dispatch normally'
    print('original game: Controls footer tags accepted; Quit keyboard/mouse suppressed; Defaults preserved')


if __name__ == '__main__':
    check(sys.argv[1])
