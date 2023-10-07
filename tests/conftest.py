import options
import pytest

def pytest_addoption(parser):
    options.add_base_options(parser.addoption)
    options.add_test_options(parser.addoption)


def pytest_collection_modifyitems(config, items):
    has_uefi_x64, has_uefi_aa64, has_bios, has_bios_iso = (
        options.check_availability(config.getoption)
    )

    for item in items:
        if "uefi_x64" in item.keywords:
            if not has_uefi_x64:
                item.add_marker(
                    pytest.mark.skip(
                        reason="missing x64 UEFI loader or firmware"
                    )
                )
            continue

        if "uefi_aarch64" in item.keywords:
            if not has_uefi_aa64:
                item.add_marker(
                    pytest.mark.skip(
                        reason="missing aa64 UEFI loader or firmware"
                    )
                )
            continue

        if not has_bios and "fat" in item.keywords:
            item.add_marker(pytest.mark.skip(reason="missing hyper installer"))
            continue

        if "iso" in item.keywords:
            if not has_bios_iso:
                item.add_marker(pytest.mark.skip(reason="missing hyper iso loader"))
                continue

            if not has_bios and "hdd" in item.keywords:
                item.add_marker(pytest.mark.skip(reason="missing hyper installer"))
