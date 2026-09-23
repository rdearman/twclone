"""
Tests for money.py: the central credits parser/formatter.

Confirmed server shapes (see src/server_ports.c, src/server_bank.c,
src/server_corporation.c, src/common.c h_format_credits):
* Most commands (bank.balance, corp balance, deposit/withdraw, equity.buy
  total_cost, dividends, par_value, trade.quote total_buy/sell_price) send a
  plain JSON integer.
* trade.buy/trade.sell receipts send total_item_value/fees/total_cost/
  credits_remaining/line "value" as a decimal string produced by
  h_format_credits(): always "<integer>.00" today (the economy has no
  sub-unit).
* trade.quote buy_price/sell_price are sent as a JSON real that is always
  integral (e.g. 42.0).
"""
import re

import pytest

from money import MoneyError, parse_credits, format_credits, format_credits_or_dash


def test_parse_plain_integer_credits():
    assert parse_credits(1234) == 1234
    assert parse_credits(0) == 0


def test_parse_string_decimal_receipt():
    # Confirmed h_format_credits() output shape.
    assert parse_credits("1234.00") == 1234
    assert parse_credits("0.00") == 0


def test_parse_integral_float_per_unit_price():
    # Confirmed trade.quote buy_price/sell_price shape: json_real of an int.
    assert parse_credits(42.0) == 42


def test_parse_never_divides_or_multiplies_by_100():
    # A naive "minor units" assumption would turn 1234 into 12.34 or 123400;
    # neither must happen for a plain integer or a "<n>.00" string.
    assert parse_credits(1234) == 1234
    assert parse_credits("1234.00") == 1234


def test_format_credits_uses_thousands_separator_and_no_binary_float_math():
    assert format_credits(1234567) == "1,234,567 cr"
    assert format_credits("1234.00") == "1,234 cr"


def test_rejects_malformed_monetary_values():
    with pytest.raises(MoneyError):
        parse_credits("not-a-number")
    with pytest.raises(MoneyError):
        parse_credits("12.50")  # genuine fractional component: unsupported
    with pytest.raises(MoneyError):
        parse_credits(None)
    with pytest.raises(MoneyError):
        parse_credits(True)  # bool is an int subtype; must not silently pass
    with pytest.raises(MoneyError):
        parse_credits("")


def test_format_credits_or_dash_degrades_gracefully():
    assert format_credits_or_dash(None) == "-"
    assert format_credits_or_dash("garbage") == "garbage"
    assert format_credits_or_dash(500) == "500 cr"


def test_money_module_never_uses_float_arithmetic_on_the_value():
    import inspect
    src = inspect.getsource(parse_credits)
    # The only floats permitted are as an input type-check / str() bridge;
    # there must be no division or multiplication against the amount.
    assert not re.search(r"value\s*[*/]\s*100", src)
    assert not re.search(r"/\s*100\.0", src)
