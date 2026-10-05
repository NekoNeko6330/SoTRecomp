#!/usr/bin/env python3
# 
#   gen_recomp.py: A script to create a recomp configuration from Zelda64
#                  decomp build output.
#   Copyright (C) 2024  Tharo
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <https://www.gnu.org/licenses/>.
#

import argparse, hashlib, os, struct, sys
from dataclasses import dataclass
from enum import auto, Enum
from typing import Any, Dict, List
from xxhash import xxh3_64 as XXH3_64

def xxhash(data):
    return XXH3_64(data, 0).intdigest()

def md5sum(data):
    return int.from_bytes(hashlib.md5(data).digest(), byteorder='big')

def prepattr(object, name, default_cb=None):
    """
    Like getattr, but if the attr does not exist it will create it and set it to the default, which is a
    callback that only runs if needed
    """
    attr = getattr(object, name, None)
    if attr is None:
        dflt = default_cb(object)
        setattr(object, name, dflt)
        attr = dflt
    return attr

def alignN(x, n):
    return (x + n-1) & ~(n-1)

def read4(data, p):
    return struct.unpack(">I", data[p:p+4])[0]

def read2(data, p):
    return struct.unpack(">H", data[p:p+2])[0]

def read1(data, p):
    return data[p]

def read4s(data, p=0):
    return bytes(data[p:p+4]).decode("ASCII")

def read_string(data, p=0, enc="ASCII"):
    end = p
    while data[end] != 0:
        end += 1
    return bytes(data[p:end]).decode(enc)

def partition_data(data, size):
    assert len(data) % size == 0
    num = len(data) // size
    for i in range(num):
        yield data[i*size:(i+1)*size]

class Array:
    TYPE_CACHE = {}

    def __class_getitem__(cls, init):
        if not isinstance(init, tuple) and len(init) != 2:
            raise Exception("Bad")
        item_type, item_num = init
        if not issubclass(item_type, StructType):
            raise Exception("Bad")
        if not isinstance(item_num, int):
            raise Exception("Bad")
        if init in Array.TYPE_CACHE:
            return Array.TYPE_CACHE[init]
        new_type = type(f"{item_type.__name__}[{item_num}]", (item_type,), {})
        new_type.IS_ARRAY = True
        new_type.ARRAY_NUM = item_num
        Array.TYPE_CACHE[init] = new_type
        return new_type

class StructType:
    SIZE = 0
    IS_ARRAY = False
    ARRAY_NUM = 1
    ALIGN = 1

class u64(int, StructType):
    SIZE = 8
    ALIGN = 8
    FMT = "Q"

class u32(int, StructType):
    SIZE = 4
    ALIGN = 4
    FMT = "I"

class u16(int, StructType):
    SIZE = 2
    ALIGN = 2
    FMT = "H"

class u8(int, StructType):
    SIZE = 1
    ALIGN = 1
    FMT = "B"

class byte(int, StructType):
    SIZE = 1
    ALIGN = 1
    FMT = "B"

class char(int, StructType):
    SIZE = 1
    ALIGN = 1
    FMT = "c"

class StructClassEndian(Enum):
    BIG = auto()
    LITTLE = auto()

def unpack_structure(data : memoryview, struct_type, endian=StructClassEndian.BIG):
    endian_prefix = ">" if endian == StructClassEndian.BIG else "<"

    total_size_bytes = 0
    greatest_align = 0

    values = []

    for field_name in struct_type.__annotations__:
        field_type = struct_type.__annotations__[field_name]
        size_bytes = field_type.SIZE * field_type.ARRAY_NUM
        align = field_type.ALIGN
        greatest_align = max(greatest_align, align)
        mydata = data[total_size_bytes:total_size_bytes+size_bytes]
        total_size_bytes += size_bytes
        total_size_bytes = alignN(total_size_bytes, align)

        # print(field_name)
        # print(field_type.SIZE, field_type.IS_ARRAY, field_type.ARRAY_NUM)
        # print(size_bytes)
        # print([hex(b) for b in mydata])

        field_value = None
        if field_type.IS_ARRAY:
            if field_type.FMT == "c":
                field_value = bytes(mydata).decode("ASCII")
            else:
                field_value = [i[0] for i in struct.iter_unpack(endian_prefix + field_type.FMT, mydata)]
        else:
            field_value = struct.unpack(endian_prefix + field_type.FMT, mydata)[0]

        assert field_value is not None
        values.append(field_value)

    total_size_bytes = alignN(total_size_bytes, greatest_align)
    # print(total_size_bytes)
    assert len(data) == total_size_bytes, (len(data),total_size_bytes)

    return struct_type(*values)

def unpack_structure_array(data, dtype, num):
    return [dtype.from_bin(dat) for dat in partition_data(data[:num * dtype.SIZE], dtype.SIZE)]

def struct_size(struct_type):
    total_size_bytes = 0
    greatest_align = 0
    for field_name in struct_type.__annotations__:
        field_type = struct_type.__annotations__[field_name]
        size_bytes = field_type.SIZE * field_type.ARRAY_NUM
        align = field_type.ALIGN
        greatest_align = max(greatest_align, align)
        total_size_bytes += size_bytes
        total_size_bytes = alignN(total_size_bytes, align)
    return alignN(total_size_bytes, greatest_align)

class StructClassMeta(type):

    def __new__(cls, name, bases, dct, endian=StructClassEndian.BIG):
        def create_attr(m : type, name : str, value : Any):
            if name in m.__dict__:
                return True
            setattr(m, name, value)
            return False

        def build_function(fn_name, fn_body, locals):
            local_vars = ", ".join(locals.keys())
            txt = f"def __create_fn__({local_vars}):\n"    + \
                f"{fn_body}\n"                           + \
                f"    return {fn_name}"
            # print(txt)
            ns = {}
            exec(txt, None, ns)
            return ns["__create_fn__"](**locals)

        m = super().__new__(cls, name, bases, dct)
        if name == "StructClass":
            return m

        annotations = dct.get("__annotations__", {})
        for base in bases:
            annotations.update(base.__dict__.get("__annotations__", {}))

        if len(annotations) == 0:
            raise Exception("No annotations")

        field_names = [field_name for field_name,field_type in annotations.items()]

        init_body      = f"    def __init__(self, {', '.join(field_names)}) -> None:\n"
        for field_name in field_names:
            init_body += f"        self.{field_name} = {field_name}\n"

        field_strings = [f"{field_name} = self.{field_name}" for field_name in field_names]
        str_body  = f"    def __str__(self) -> str:\n"
        str_body += f"        return \"{m.__name__}({', '.join(field_strings)})\"\n"

        from_bin_body  = f"    def from_bin(data : memoryview) -> \"{m.__name__}\":\n"
        from_bin_body += f"        return unpack_structure(data, {m.__name__}, StructClassEndian.{endian.name})\n"

        create_attr(m, "__init__", build_function("__init__", init_body, {}))
        create_attr(m, "__str__", build_function("__str__", str_body, {}))
        create_attr(m, "from_bin", build_function("from_bin", from_bin_body, {}))
        create_attr(m, "SIZE", struct_size(m))
        return m

class StructClass(metaclass=StructClassMeta):
    SIZE = 0

    def __init__(self, *args) -> None:
        raise NotImplementedError() # Constructed by metaclass, here for nicer linting only

    def __str__(self) -> str:
        raise NotImplementedError() # Constructed by metaclass, here for nicer linting only

    @staticmethod
    def from_bin(data : memoryview) -> "StructClass":
        raise NotImplementedError() # Constructed by metaclass, here for nicer linting only

SHT_NOBITS = 8

class ELF32_EHDR(StructClass):
    e_ident : Array[u8,16]
    e_type : u16
    e_machine : u16
    e_version : u32
    e_entry : u32
    e_phoff : u32
    e_shoff : u32
    e_flags : u32
    e_ehsize : u16
    e_phentsize : u16
    e_phnum : u16
    e_shentsize : u16
    e_shnum : u16
    e_shstrndx : u16

    def read_shdr(self, i : int) -> "ELF32_SHDR":
        p = self.e_shoff + i * self.e_shentsize
        shdr : ELF32_SHDR = ELF32_SHDR.from_bin(self.data[p:p+ELF32_SHDR.SIZE])
        shdr.data = None
        if shdr.sh_type != SHT_NOBITS:
            shdr.data = self.data[shdr.sh_offset:shdr.sh_offset+shdr.sh_size]
            assert len(shdr.data) == shdr.sh_size
        return shdr

    def read_shstrtab(self) -> "ELF32_SHDR":
        return self.read_shdr(self.e_shstrndx)

class ELF32_SHDR(StructClass):
    sh_name : u32
    sh_type : u32
    sh_flags : u32
    sh_addr : u32
    sh_offset : u32
    sh_size : u32
    sh_link : u32
    sh_info : u32
    sh_addralign : u32
    sh_entsize : u32

    def read_name(self, shstrtab : "ELF32_SHDR") -> str:
        return prepattr(self, "name", lambda _self : read_string(shstrtab.data, _self.sh_name, enc="latin1"))

class ELF32_SYM(StructClass):
    st_name : u32
    st_value : u32
    st_size : u32
    st_info : u8
    st_other : u8
    st_shndx : u16

    def read_name(self, strtab : "ELF32_SHDR") -> str:
        return prepattr(self, "name", lambda _self : read_string(strtab.data, _self.st_name, enc="latin1"))

class N64_ROM_HEADER(StructClass):
    endian : u8
    pi_dom1_cfg : Array[u8,3]
    sysclk_rate : u32
    entrypoint : u32
    libultra_ver : u32
    checksum : u64
    _padding_x18 : Array[u8,8]
    rom_name : Array[char,0x14]
    _padding_x34 : Array[u8,7]
    medium : u8
    game_id : Array[u8,2]
    region : u8
    game_rev : u8

@dataclass
class UCodeInfo:
    name : str                              # ucode symbol without a "TextStart" or similar suffix
    text_addr : int                         # base imem address of .text
    text_md5 : int                          # md5 of text for verif
    data_md5 : int                          # md5 of data for verif
    extra_indirect_branch_targets : tuple   # any DMEM labels that point into IMEM should be listed here

def main(elf_path : str, windows_elf_path: str):
    ROM_PATH = elf_path.replace(".elf", ".z64")
    TOML_OUTNAME = "indigo.toml"
    OVERLAYS_OUTNAME = "overlays.txt"
    UCODES_FOR_RECOMP = [
        UCodeInfo("aspMain", 0x04001000,
                  0x316046D1748C3487EF8792D42CC6B433,
                  0xAD2D1D7E8C2AFD1FB7E4267E57597EB7,
                  (0x1F68, 0x1230, 0x114C, 0x1F18, 0x1E2C, 0x14F4, 0x1E9C, 0x1CB0,
                   0x117C, 0x17CC, 0x11E8, 0x1AA4, 0x1B34, 0x1190, 0x1C5C, 0x1220,
                   0x1784, 0x1830, 0x1A20, 0x1884, 0x1A84, 0x1A94, 0x1A48, 0x1BA0)),
        UCodeInfo("njpgdspMain", 0x04001080,
                  0x1CAB4DC7403C218956ADC82DFFC624C0,
                  0xCF5303B2528507DAD6DA93DF2A52A01F,
                  ()),
    ]

    # Read in elf file

    with open(elf_path, "rb") as infile:
        elf = memoryview(infile.read())

    # Read header

    ehdr : ELF32_EHDR = ELF32_EHDR.from_bin(elf[:ELF32_EHDR.SIZE])
    assert bytes(ehdr.e_ident[0:4]) == b"\x7fELF"
    assert ehdr.e_shentsize == ELF32_SHDR.SIZE
    ehdr.data = elf

    # Read sections

    shstrtab : ELF32_SHDR = ehdr.read_shstrtab()
    shdrs = [ehdr.read_shdr(i) for i in range(ehdr.e_shnum)]

    # Find the symtab + strtab sections and makerom program segment for the ROM header

    symtab : ELF32_SHDR = None
    strtab : ELF32_SHDR = None
    makerom : memoryview = None
    n = 0

    for shdr in shdrs:
        if shdr.read_name(shstrtab) == ".symtab":
            assert symtab is None
            symtab = shdr
            n += 1
        elif shdr.read_name(shstrtab) == ".strtab":
            assert strtab is None
            strtab = shdr
            n += 1
        elif shdr.read_name(shstrtab) == "..makerom":
            assert makerom is None
            makerom = shdr.data
            n += 1

        if n == 3:
            break
    else:
        assert False, "Did not find all required sections"

    # Collect symbols

    symbols : List[ELF32_SYM] = list(sorted((ELF32_SYM.from_bin(symtab.data[p:p+symtab.sh_entsize]) for p in range(0, symtab.sh_size, symtab.sh_entsize)), key = lambda sym : sym.st_value))
    symbols_forname : Dict[str,ELF32_SYM] = { sym.read_name(strtab) : sym for sym in symbols }

    # Read ROM header

    rom_header : N64_ROM_HEADER = N64_ROM_HEADER.from_bin(makerom[:N64_ROM_HEADER.SIZE])

    # Write Recomp TOML config

# Enable strict patch mode, validates that patched symbols exist and that non-patch functions aren't symbols.
# strict_patch_mode = true
    TOML = f"""\
# Config file for "{rom_header.rom_name}" Recompilation.

[input]
entrypoint = 0x{rom_header.entrypoint:08X}
# Paths are relative to the location of this config file.
elf_path = "{windows_elf_path}"
output_func_path = "RecompiledFuncs"
relocatable_sections_path = "{OVERLAYS_OUTNAME}"
recomp_include = "#include \\"recomp.h\\""

# Enable strict patch mode, validates that patched symbols exist and that non-patch functions aren't symbols.
strict_patch_mode = true


[patches]
stubs = [
    "RcpUtils_PrintRegisterStatus",
    "RcpUtils_Reset",
    "osDriveRomInit"
]

ignored = [
    # This is only a dummy function to inform recomp about overlay loading
    "RECOMP_load_overlays_recomp",
    # These are supposed to be corrupted and corrupted_init in pfschecker.c but they aren't named yet so aren't auto-ignored
    "func_80105788",
    "func_80105A60",
]
"""

    with open(TOML_OUTNAME, "w") as outfile:
        outfile.write(TOML)

    # Enumerate overlay segments

    ovl_sections = []

    for shdr in shdrs:
        vaddr = shdr.sh_addr
        name = shdr.read_name(shstrtab)

        if name.startswith("..ovl_"):
            assert vaddr >= 0x80800000
            if not name.endswith(".bss"):
                # Don't include overlays with no .text, recompiler doesn't need them and it struggles with them
                if symbols_forname[f"_{name[2:]}SegmentTextSize"].st_value != 0:
                    ovl_sections.append(name)
        else:
            assert vaddr < 0x80800000

    # Write overlays list

    with open(OVERLAYS_OUTNAME, "w") as outfile:
        outfile.write("\n".join(ovl_sections) + "\n")

    # Extract ucodes to bin for RSPRecomp

    for ucode in UCODES_FOR_RECOMP:
        # Locate text/data start/end symbols

        text_start = symbols_forname.get(f"{ucode.name}TextStart", None)
        text_end = symbols_forname.get(f"{ucode.name}TextEnd", None)
        data_start = symbols_forname.get(f"{ucode.name}DataStart", None)
        data_end = symbols_forname.get(f"{ucode.name}DataEnd", None)
        
        all_syms = (text_start,text_end,data_start,data_end)
        
        if all(sym is None for sym in all_syms):
            print(f"No symbols for ucode {ucode.name}, assuming it is not required.")
            continue
        elif any(sym is None for sym in all_syms):
            assert False, f"Could not find all relevant symbols for ucode {ucode.name}"

        text_start_v = text_start.st_value
        text_end_v = text_end.st_value
        data_start_v = data_start.st_value
        data_end_v = data_end.st_value

        # Locate text/data section contents

        ucode_text = None
        ucode_data = None

        for shdr in shdrs:
            start = shdr.sh_addr
            end = start + shdr.sh_size

            if text_start_v in range(start,end):
                assert ucode_text is None , "Overlapping address ranges present in elf file, cannot find ucode text"
                offset = text_start_v - start
                # print(f"text start 0x{text_start_v:08X} is 0x{offset:08X} bytes in {shdr.read_name(shstrtab)}[0x{start:08X}:0x{end:08X}]")
                assert text_end_v in range(start,end+1) , f"text end 0x{text_end_v:08X} out of range [0x{start:08X}:0x{end:08X}]"
                ucode_text = shdr.data[offset:text_end_v - start]

            if data_start_v in range(start,end):
                assert ucode_data is None , "Overlapping address ranges present in elf file, cannot find ucode data"
                offset = data_start_v - start
                # print(f"data start 0x{data_start_v:08X} is 0x{offset:08X} bytes in {shdr.read_name(shstrtab)}[0x{start:08X}:0x{end:08X}]")
                assert data_end_v in range(start,end+1) , f"data end 0x{data_end_v:08X} out of range [0x{start:08X}:0x{end:08X}]"
                ucode_data = shdr.data[offset:data_end_v - start]

        assert ucode_text is not None and ucode_data is not None

        # Verify that the section contents match the expectation

        assert md5sum(ucode_text) == ucode.text_md5
        assert md5sum(ucode_data) == ucode.data_md5

        # Write the .text binary

        with open(f"rsp/{ucode.name}.text.bin", "wb") as outfile:
            outfile.write(ucode_text)

        # Write the toml for this ucode

        text_offset = 0
        text_size = len(ucode_text)
        text_address = ucode.text_addr
        rom_file_path = f"rsp/{ucode.name}.text.bin"
        output_file_path = f"rsp/{ucode.name}.cpp"
        output_function_name = ucode.name
        extra_indirect_branch_targets = ucode.extra_indirect_branch_targets
        unsupported_instructions = []

        ucode_toml = f"""\
text_offset = {text_offset}
text_size = 0x{text_size:X}
text_address = 0x{text_address:08X}
rom_file_path = "{rom_file_path}"
output_file_path = "{output_file_path}"
output_function_name = "{output_function_name}"
extra_indirect_branch_targets = [
    {", ".join(f"0x{i:04X}" for i in extra_indirect_branch_targets)}
]"""
        with open(f"{ucode.name}.toml", "w") as outfile:
            outfile.write(ucode_toml)

        # Write rom_config.h for the menu

        with open(ROM_PATH, "rb") as infile:
            rom_data = infile.read()

        game_id = "recomp::Game::MM"
        rom_hash = xxhash(rom_data)
        rom_save = os.path.basename(elf_path).replace(".elf", ".z64")
        rom_title = rom_header.rom_name.strip()

        with open("rom_config.h", "w") as outfile:
            outfile.write(f"{{ {game_id}, {{ 0x{rom_hash:016X}ULL, u8\"{rom_save}\", \"{rom_title}\" }}}},\n")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
