"""Place the optional whiskey patch alongside the other vanilla replacers."""
import xml.etree.ElementTree as ET

def add_whiskey_option(config):
    name = 'Fallout 3 Style Whiskey'
    steps = config.find('installSteps')
    for step in list(steps):
        if step.get('name') == 'Fallout 3 Style Whiskey compatibility':
            steps.remove(step)
    step = next(s for s in steps if s.get('name') == '3. Vanilla replacers')
    groups = step.find('optionalFileGroups')
    for group in list(groups):
        if any(p.get('name') == name for p in group.iter('plugin')):
            groups.remove(group)
    group = ET.SubElement(groups, 'group', name='Whiskey bottle replacer', type='SelectAny')
    plugins = ET.SubElement(group, 'plugins', order='Explicit')
    plugin = ET.SubElement(plugins, 'plugin', name=name)
    ET.SubElement(plugin, 'description').text = 'Requires Fallout 3 Style Whiskey.\nhttps://www.nexusmods.com/fallout4/mods/53251'
    ET.SubElement(plugin, 'image', path='fomod/images/Fallout3Whiskey.jpg')
    files = ET.SubElement(plugin, 'files')
    ET.SubElement(files, 'folder', source='components/Fallout3Whiskey', destination='', priority='20')
    ET.SubElement(ET.SubElement(plugin, 'typeDescriptor'), 'type', name='Optional')
