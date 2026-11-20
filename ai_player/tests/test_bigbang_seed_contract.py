from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_bigbang_accepts_a_reproducible_universe_seed():
    source = (ROOT / "src/bigbang_pg_main.c").read_text()
    assert 'json_object_get (jcfg, "universe_seed")' in source
    assert '"universe-seed", required_argument, 0, 1007' in source
    assert "srand (have_universe_seed ? universe_seed" in source
    assert '"set_universe_seed"' in source
    assert 'strcmp (key, "universe_seed") == 0' in source


def test_bigbang_can_include_tunnel_sectors_inside_an_exact_target_universe():
    source = (ROOT / "src/bigbang_pg_main.c").read_text()
    assert 'json_object_get (jcfg, "exact_sector_count")' in source
    assert '"generate_tunnels_exact_sector_count"' in source
    assert "int initial_sector_count = sectors" in source
    assert "if (!exact_sector_count)" in source
