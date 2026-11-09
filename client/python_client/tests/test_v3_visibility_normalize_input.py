"""Fail-closed decoder coverage for VIS-01 client visibility handling."""

from protocol import normalize_sector


def test_normalize_sector_rejects_non_object_payloads_without_dereferencing():
    for payload in (None, [], ["remote sector"], "remote sector", 12, True):
        assert normalize_sector(payload) == {}


def test_normalize_sector_does_not_cache_schema_mismatched_name():
    normalized = normalize_sector({"sector_id": 12, "name": {"secret": "remote"}})

    assert normalized["id"] == 12
    assert normalized["name"] == "Sector 12"


def test_normalize_sector_rejects_mixed_port_rows_as_visibility_evidence():
    normalized = normalize_sector({
        "sector_id": 12,
        "ports": [{"id": 8, "name": "Known"}, "schema-mismatched row"],
    })

    assert normalized["port"] == {"present": None}

