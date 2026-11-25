import argparse
import json
import os
from pathlib import Path
import sys

parser = argparse.ArgumentParser(description='Verify a merged Mass Item Selector Shipping ZIP and optionally its existing installation, read-only.')
parser.add_argument('archive', type=Path, help='Merged release ZIP, or flat development ZIP with --target')
parser.add_argument('--shared-tools', type=Path, default=os.environ.get('SATISFACTORY_SHARED_TOOLS'))
parser.add_argument('--engine-path', type=Path, default=os.environ.get('SATISFACTORY_ENGINE_PATH'), required=not bool(os.environ.get('SATISFACTORY_ENGINE_PATH')), help='Compatible CSS engine for mandatory container/privacy inspection')
parser.add_argument('--target', choices=('Windows', 'WindowsServer'), help='Verify a single-target development ZIP instead of the merged release')
parser.add_argument('--report-root', type=Path, help='Local audit output directory')
parser.add_argument('--game-path', type=Path, help='Compare the existing GameFeatures installation with the selected target')
parser.add_argument('--installed-target', choices=('Windows', 'WindowsServer'), default='Windows')
parser.add_argument('--install-report', type=Path, help='Write installed-file hashes as JSON; requires --game-path')
args = parser.parse_args()
if args.install_report and not args.game_path:
    parser.error('--install-report requires --game-path')
shared_tools = args.shared_tools
if shared_tools is None:
    project = next((path for path in Path(__file__).resolve().parents if (path / 'FactoryGame.uproject').is_file()), None)
    if project is None:
        parser.error('Provide --shared-tools outside an SML Starter Project')
    shared_tools = project / '.mod-development/Tools'
if not all((shared_tools / name).is_file() for name in ('release_archive.py', 'installed_package.py', 'package_audit.py', 'Audit-Package.ps1', 'Inspect-CookedContent.ps1', 'Test-Privacy.ps1')):
    parser.error('Shared release tools are missing; provide --shared-tools')
sys.path.insert(0, str(shared_tools))
from release_archive import checked_archive
from installed_package import verify_install
from package_audit import audit_package

if sys.flags.optimize:
    parser.error('Run without -O: policy assertions must remain enabled')

archive = args.archive
source_descriptor = json.loads((Path(__file__).resolve().parents[1] / 'MassItemSelector.uplugin').read_text(encoding='utf-8-sig'))
expected_sml = next(plugin['SemVersion'] for plugin in source_descriptor['Plugins'] if plugin['Name'] == 'SML')
expected_targets = {'Windows', 'WindowsServer'}
with checked_archive(archive) as release:
    report_root = args.report_root or shared_tools.parent / 'MassItemSelector/package-audit'
    containers, private, report = audit_package(release, args.engine_path, report_root)
    print(f'Container listings, extracted data and privacy diagnostics: {report}', flush=True)
    names = release.namelist()
    if args.target:
        expected_targets = {args.target}
    else:
        assert {name.split('/')[0] for name in names} == expected_targets, 'Unexpected release targets'
    expected_dlls = {
        'Windows/Binaries/Win64/FactoryGameSteam-MassItemSelector-Win64-Shipping.dll',
        'Windows/Binaries/Win64/FactoryGameEGS-MassItemSelector-Win64-Shipping.dll',
        'WindowsServer/Binaries/Win64/FactoryServer-MassItemSelector-Win64-Shipping.dll',
    }
    if args.target:
        expected_dlls = {name.split('/', 1)[1] for name in expected_dlls if name.startswith(args.target + '/')}
        if args.target == 'Windows':
            # Alpakit Dev selects Steam or EGS from the installation; releases require both.
            actual_dlls = {name for name in names if name.endswith('.dll')}
            assert actual_dlls and actual_dlls <= expected_dlls, 'Unexpected development binaries'
            expected_dlls = actual_dlls
    assert {name for name in names if name.endswith('.dll')} == expected_dlls, 'Unexpected release binaries'
    expected_files = set(expected_dlls)
    for dll in expected_dlls:
        expected_files.add(dll.replace('-MassItemSelector', '').replace('.dll', '.modules'))
    for target in expected_targets:
        prefix = '' if args.target else target + '/'
        expected_files.update({prefix + 'MassItemSelector.uplugin', prefix + 'Resources/Icon128.png'})
        expected_files.update(prefix + f'Content/Paks/{target}/MassItemSelectorFactoryGame-{target}.{extension}' for extension in ('pak', 'ucas', 'utoc'))
        descriptor = json.loads(release.read(prefix + 'MassItemSelector.uplugin').decode('utf-8-sig'))
        assert descriptor['VersionName'] == descriptor['SemVersion'] == source_descriptor['VersionName'] == source_descriptor['SemVersion']
        assert descriptor['GameFeature'] is True
        assert descriptor['GameVersion'] == source_descriptor['GameVersion']
        assert any(plugin['Name'] == 'SML' and plugin['SemVersion'] == expected_sml for plugin in descriptor['Plugins'])
    expected_pak = {'AssetRegistry.bin', 'Config/AccessTransformers.ini', 'Config/PluginSettings.ini',
                    'Content/Localization/MassItemSelector/en/MassItemSelector.locres',
                    'Content/Localization/MassItemSelector/MassItemSelector.locmeta'}
    content_prefix = '../../../FactoryGame/Mods/GameFeatures/MassItemSelector/Content/'
    expected_cooked = {content_prefix + name + '.uasset' for name in (
        'MassItemSelector', 'Inputs/IA_Lasso', 'Inputs/IA_SelectionFilter',
        'Inputs/IMC_MassItemSelector', 'Hooks/Hook_DismantleHints_MassItemSelector')}
    expected_cooked.add('<ContainerHeader>')
    errors = []
    if set(names) != expected_files:
        errors.append(f'Archive contents differ: missing={sorted(expected_files - set(names))}, extra={sorted(set(names) - expected_files)}')
    if len(containers) != len(expected_targets):
        errors.append('Unexpected number of cooked containers')
    for container in containers:
        if set(container['PakFiles']) != expected_pak or len(container['PakFiles']) != len(expected_pak):
            errors.append(f"Unexpected pak contents: {container['PakFiles']}")
        if set(container['CookedFiles']) != expected_cooked or len(container['CookedFiles']) != len(expected_cooked):
            errors.append(f"Unexpected cooked assets: {container['CookedFiles']}")
    if not private:
        errors.append('Privacy scan failed; see audit.log')
    for name in names:
        if name.endswith('.dll'):
            data = release.read(name)
            for marker in ('MassItemSelector.Selection.', 'FMISNativeCounterTest'):
                assert marker.encode() not in data and marker.encode('utf-16-le') not in data, 'Automation marker: ' + name
    if args.game_path:
        installed_root = args.game_path / 'FactoryGame/Mods/GameFeatures/MassItemSelector'
        result = verify_install(release, installed_root, '' if args.target else args.installed_target)
        install_report = args.install_report or report / 'installed-hashes.json'
        install_report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(f"Installed {args.target or args.installed_target} package verified: exact file set and SHA-256 for {result['file_count']} files.")
        if result['installed_metadata']:
            print('Recognized installed metadata: .smm')
    if errors:
        raise ValueError('\n'.join(errors))
    print(f"Verified runtime-only ZIP and cooked contents, privacy, Shipping binaries, version {source_descriptor['VersionName']}, Game Feature metadata, SML compatibility and absence of automation markers.")
print(f'{archive}\n{archive.stat().st_size:,} bytes')
