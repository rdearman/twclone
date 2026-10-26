import pytest

from app.config import Settings


def test_public_origin_is_normalized_and_production_cookie_defaults_secure():
    settings = Settings.from_env(
        {"GATEWAY_ENV": "production", "GATEWAY_PUBLIC_ORIGIN": "https://game.example/"}
    )
    assert settings.public_origin == "https://game.example"
    assert settings.session_cookie_secure is True


def test_public_origin_rejects_paths_and_non_http_schemes():
    with pytest.raises(ValueError, match="GATEWAY_PUBLIC_ORIGIN"):
        Settings.from_env({"GATEWAY_PUBLIC_ORIGIN": "https://game.example/path"})
    with pytest.raises(ValueError, match="GATEWAY_PUBLIC_ORIGIN"):
        Settings.from_env({"GATEWAY_PUBLIC_ORIGIN": "javascript://game.example"})


def test_cross_site_cookie_requires_secure_transport():
    settings = Settings.from_env({
        "GATEWAY_SESSION_COOKIE_SAMESITE": "none",
        "GATEWAY_SESSION_COOKIE_SECURE": "true",
    })
    assert settings.session_cookie_samesite == "none"
    assert settings.session_cookie_secure is True
    with pytest.raises(ValueError, match="SameSite=None requires"):
        Settings.from_env({"GATEWAY_SESSION_COOKIE_SAMESITE": "none"})
