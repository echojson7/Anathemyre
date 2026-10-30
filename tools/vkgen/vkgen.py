#!/usr/bin/env python3
# Генератор src/render/vk.h из реестра Khronos vk.xml: только то, что перечислено в want.txt,
# и типы, от которых оно зависит. Игра не тянет сторонних заголовков: X11 и windows.h тоже не нужны.
# python3 vkgen.py vk.xml want.txt out.h
import sys
import xml.etree.ElementTree as ET

# Типы ОС из Xlib и windows.h: вместо их заголовков — совместимые по размеру
PLATFORM_TYPES = {
    'Display': 'typedef struct _XDisplay Display;',
    'Window': 'typedef unsigned long Window;',
    'VisualID': 'typedef unsigned long VisualID;',
    'HINSTANCE': 'typedef struct HINSTANCE__ *HINSTANCE;',
    'HWND': 'typedef struct HWND__ *HWND;',
}
C_TYPES = {'void', 'char', 'float', 'double', 'int', 'size_t',
           'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t', 'int8_t', 'int16_t', 'int32_t', 'int64_t'}


def for_vulkan(el):
    api = el.get('api')
    return api is None or 'vulkan' in api.split(',')


def text_of(el):
    # Текст декларации без <comment>
    out = [el.text or '']
    for ch in el:
        if ch.tag != 'comment':
            out.append(text_of(ch))
        out.append(ch.tail or '')
    return ''.join(out)


def decl(el):
    return ' '.join(text_of(el).split())


def main():
    xml_path, want_path, out_path = sys.argv[1:4]
    reg = ET.parse(xml_path).getroot()

    want_cmds, want_types, want_exts = [], [], []
    for line in open(want_path, encoding='utf-8'):
        line = line.split('#', 1)[0].split()
        if not line:
            continue
        kind, name = line
        {'command': want_cmds, 'type': want_types, 'ext': want_exts}[kind].append(name)

    types = {}
    for t in reg.find('types'):
        if t.tag != 'type' or not for_vulkan(t):
            continue
        name = t.get('name') or (t.find('name').text if t.find('name') is not None else None)
        if name:
            types[name] = t

    commands, cmd_alias = {}, {}
    for c in reg.find('commands'):
        if not for_vulkan(c):
            continue
        if c.get('alias'):
            cmd_alias[c.get('name')] = c.get('alias')
        else:
            commands[c.find('proto/name').text] = c

    # Значения перечислений: из <enums> и из всех расширений и версий, что дополняют их
    enum_blocks = {}
    api_consts = []
    for e in reg.findall('enums'):
        if e.get('name') == 'API Constants':
            api_consts = [x for x in e.findall('enum')]
            continue
        vals = {}
        for v in e.findall('enum'):
            if for_vulkan(v):
                vals[v.get('name')] = v
        enum_blocks[e.get('name')] = (e, vals)

    def add_ext_enums(req, extnumber):
        for v in req.findall('enum'):
            target = v.get('extends')
            if not target or not for_vulkan(v) or target not in enum_blocks:
                continue
            vals = enum_blocks[target][1]
            if v.get('name') in vals:
                continue
            if v.get('offset') is not None:
                num = int(v.get('extnumber') or extnumber)
                val = 1000000000 + (num - 1) * 1000 + int(v.get('offset'))
                if v.get('dir') == '-':
                    val = -val
                v = ET.Element('enum', {'name': v.get('name'), 'value': str(val)})
            vals[v.get('name')] = v

    for f in reg.findall('feature'):
        if 'vulkan' not in f.get('api', 'vulkan').split(','):
            continue
        for req in f.findall('require'):
            add_ext_enums(req, 0)
    ext_consts = {}
    for x in reg.find('extensions'):
        sup = x.get('supported', '')
        # Предварительные (beta) расширения не берём
        if 'vulkan' not in sup.split(',') or x.get('provisional') == 'true':
            continue
        for req in x.findall('require'):
            if not for_vulkan(req):
                continue
            add_ext_enums(req, x.get('number'))
            if x.get('name') in want_exts:
                for v in req.findall('enum'):
                    if v.get('extends') is None and v.get('value') is not None:
                        ext_consts[v.get('name')] = v.get('value')

    # Замыкание по зависимостям
    need = []
    seen = set()

    def use_type(name):
        if name in seen or name in C_TYPES:
            return
        seen.add(name)
        if name in PLATFORM_TYPES:
            need.append(name)
            return
        t = types.get(name)
        if t is None:
            raise SystemExit(f'vkgen: тип {name} не найден')
        if t.get('alias'):
            use_type(t.get('alias'))
            need.append(name)
            return
        cat = t.get('category')
        if cat in ('struct', 'union'):
            for m in t.findall('member'):
                if for_vulkan(m):
                    use_type(m.find('type').text)
        elif cat == 'bitmask':
            bits = t.get('requires') or t.get('bitvalues')
            if bits:
                use_type(bits)
            use_type(t.find('type').text)
        elif cat == 'funcpointer':
            for sub in t.findall('type'):
                use_type(sub.text)
        elif cat == 'basetype':
            sub = t.find('type')
            if sub is not None:
                use_type(sub.text)
        need.append(name)

    cmd_list = []
    for c in want_cmds:
        real = cmd_alias.get(c, c)
        if real not in commands:
            raise SystemExit(f'vkgen: функция {c} не найдена')
        cmd_list.append((c, commands[real]))
        cmd = commands[real]
        use_type(cmd.find('proto/type').text)
        for p in cmd.findall('param'):
            if for_vulkan(p):
                use_type(p.find('type').text)
    for t in want_types:
        use_type(t)

    by_cat = {}
    for n in need:
        cat = 'platform' if n in PLATFORM_TYPES else types[n].get('category')
        if n not in PLATFORM_TYPES and types[n].get('alias'):
            cat = 'alias'
        by_cat.setdefault(cat, []).append(n)

    o = []
    w = o.append
    w('// Сгенерировано tools/vkgen из vk.xml (Vulkan-Docs, версия в gen.sh) — не править руками.')
    w('// Только функции и типы из tools/vkgen/want.txt. Функции загружаются через vkGetInstanceProcAddr.')
    w('#ifndef ANA_VK_H')
    w('#define ANA_VK_H')
    w('')
    w('#include <stddef.h>')
    w('#include <stdint.h>')
    w('')
    w('_Static_assert(sizeof(void *) == 8, "vk.h: только 64-битные цели (дескрипторы — указатели)");')
    w('')
    w('#ifdef _WIN32')
    w('#define VKAPI_PTR __stdcall')
    w('#else')
    w('#define VKAPI_PTR')
    w('#endif')
    w('#define VK_MAKE_API_VERSION(variant, major, minor, patch) \\')
    w('    ((((uint32_t)(variant)) << 29U) | (((uint32_t)(major)) << 22U) | (((uint32_t)(minor)) << 12U) | ((uint32_t)(patch)))')
    w('#define VK_API_VERSION_MAJOR(v) (((uint32_t)(v) >> 22U) & 0x7FU)')
    w('#define VK_API_VERSION_MINOR(v) (((uint32_t)(v) >> 12U) & 0x3FFU)')
    w('#define VK_API_VERSION_PATCH(v) ((uint32_t)(v) & 0xFFFU)')
    w('#define VK_API_VERSION_1_3 VK_MAKE_API_VERSION(0, 1, 3, 0)')
    w('#define VK_NULL_HANDLE 0')
    hv = types['VK_HEADER_VERSION']
    w('#define VK_HEADER_VERSION ' + decl(hv).split()[-1])
    w('')
    for c in api_consts:
        if c.get('alias'):
            w(f'#define {c.get("name")} {c.get("alias")}')
        else:
            w(f'#define {c.get("name")} ({c.get("value")})')
    w('')
    for n, v in sorted(ext_consts.items()):
        w(f'#define {n} {v}')
    w('')

    for n in by_cat.get('platform', []):
        w(PLATFORM_TYPES[n])
    for n in by_cat.get('basetype', []):
        w(decl(types[n]))
    w('')
    for n in by_cat.get('handle', []):
        w(f'typedef struct {n}_T *{n};')
    w('')

    def enum_value(v):
        if v.get('alias'):
            return v.get('alias')
        if v.get('bitpos') is not None:
            return int(v.get('bitpos')), True
        return v.get('value'), False

    for n in by_cat.get('enum', []):
        blk = enum_blocks.get(n)
        if blk is None:
            # Пустой набор битов (пока без значений)
            w(f'typedef enum {n} {{ {n.upper()}_MAX_ENUM_ = 0x7FFFFFFF }} {n};')
            continue
        e, vals = blk
        # Синонимы после основных значений: они ссылаются на них
        vals = dict(sorted(vals.items(), key=lambda kv: kv[1].get('alias') is not None))
        wide = e.get('bitwidth') == '64'
        if wide:
            w(f'typedef uint64_t {n};')
            for vn, v in vals.items():
                if v.get('alias'):
                    w(f'#define {vn} {v.get("alias")}')
                elif v.get('bitpos') is not None:
                    w(f'#define {vn} (1ULL << {v.get("bitpos")})')
                else:
                    w(f'#define {vn} ({v.get("value")}ULL)')
            continue
        w(f'typedef enum {n} {{')
        for vn, v in vals.items():
            if v.get('alias'):
                w(f'    {vn} = {v.get("alias")},')
            elif v.get('bitpos') is not None:
                bp = int(v.get('bitpos'))
                if bp >= 31:
                    raise SystemExit(f'vkgen: {vn} не влезает в int')
                w(f'    {vn} = 0x{1 << bp:08X},')
            else:
                w(f'    {vn} = {v.get("value")},')
        w(f'    {n}_MAX_ENUM_ = 0x7FFFFFFF')
        w(f'}} {n};')
    w('')
    for n in by_cat.get('bitmask', []):
        w(decl(types[n]))
    w('')
    structs = by_cat.get('struct', []) + by_cat.get('union', [])
    for n in structs:
        kw = types[n].get('category')
        w(f'typedef {kw} {n} {n};')
    w('')
    for n in by_cat.get('funcpointer', []):
        w(decl(types[n]))
    w('')
    # Структуры в порядке замыкания: члены по значению уже определены раньше
    for n in [x for x in need if x in structs]:
        t = types[n]
        w(f'{t.get("category")} {n} {{')
        for m in t.findall('member'):
            if for_vulkan(m):
                w('    ' + decl(m) + ';')
        w('};')
    w('')
    for n in by_cat.get('alias', []):
        w(f'typedef {types[n].get("alias")} {n};')
    w('')

    for name, c in cmd_list:
        ret = decl(c.find('proto')).rsplit(' ', 1)[0].replace(' *', '*')
        params = ', '.join(decl(p) for p in c.findall('param') if for_vulkan(p)) or 'void'
        w(f'typedef {ret} (VKAPI_PTR *PFN_{name})({params});')
    w('')

    # Списки для загрузки: X(имя)
    glob, inst, dev = [], [], []
    for name, c in cmd_list:
        if name == 'vkGetInstanceProcAddr':
            continue
        first = c.find('param/type').text if c.find('param/type') is not None else ''
        if name in ('vkCreateInstance', 'vkEnumerateInstanceVersion',
                    'vkEnumerateInstanceExtensionProperties', 'vkEnumerateInstanceLayerProperties'):
            glob.append(name)
        elif first in ('VkDevice', 'VkQueue', 'VkCommandBuffer') and name != 'vkGetDeviceProcAddr':
            dev.append(name)
        else:
            inst.append(name)
    for title, lst in (('GLOBAL', glob), ('INSTANCE', inst), ('DEVICE', dev)):
        w(f'#define VK_{title}_FUNCS(X) \\')
        for name in lst:
            w(f'    X({name}) \\')
        w('')
    w('#endif')

    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(o) + '\n')


if __name__ == '__main__':
    main()
