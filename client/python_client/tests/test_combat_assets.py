"""COM-02 client handling for authoritative deployed defense assets."""

import client


def test_deployment_rows_require_non_coerced_positive_asset_ids():
    rows = client._authoritative_deployment_entries({"entries": [
        {"asset_id": "22", "count": 4},
        {"asset_id": True, "count": 4},
        {"asset_id": 0, "count": 4},
        {"asset_id": 23, "count": 4},
    ]})

    assert rows == [{"asset_id": 23, "count": 4}]


def test_malformed_deployment_list_is_empty():
    assert client._authoritative_deployment_entries({"entries": "not-a-list"}) == []
