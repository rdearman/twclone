"""Central money parsing/formatting for the Trade Wars client.

The server's game economy is integer credits. Most commands send/receive a
plain JSON integer. A small number of trade receipt fields
(``total_item_value``, ``fees``, ``total_cost``, ``credits_remaining`` on
``trade.buy``/``trade.sell``, and the per-line ``value`` field) are instead
sent as decimal strings produced by the server's ``h_format_credits()``
(always ``"<integer>.00"`` today, since the economy has no sub-unit).
A couple of per-unit price fields (``trade.quote`` ``buy_price``/
``sell_price``) are sent as a JSON real that is always integral.

This module gives every command a single, non-lossy way to turn any of
those confirmed shapes into a plain ``int`` number of credits, and back
into consistent display text. It never uses binary floating point for the
conversion itself (parsing routes through :class:`decimal.Decimal`), and it
does not invent a minor-unit (cents) scale that the server does not use:
values are never divided or multiplied by 100.
"""

from decimal import Decimal, InvalidOperation
from typing import Union

Credits = Union[int, float, str]


class MoneyError(ValueError):
    """Raised when a value cannot be interpreted as a credits amount."""


def parse_credits(value: Credits) -> int:
    """Parse a confirmed server/user credits representation into ``int``.

    Accepts:
    * a plain ``int`` (the normal case for almost every command);
    * a decimal string such as ``"1234.00"`` (trade receipt fields);
    * a JSON real that is exactly integral, e.g. ``42.0`` (trade.quote
      per-unit prices).

    Rejects ``bool`` (which is a subtype of ``int`` in Python), any value
    with a genuine fractional component, and anything else that isn't a
    number/numeral string. Never performs binary float arithmetic on the
    value; float input is converted via ``str()`` before going through
    ``Decimal`` so no precision is introduced or lost.
    """
    if isinstance(value, bool):
        raise MoneyError(f"invalid credits value: {value!r}")

    if isinstance(value, int):
        return value

    if isinstance(value, float):
        decimal_value = Decimal(str(value))
    elif isinstance(value, str):
        text = value.strip()
        if not text:
            raise MoneyError("credits value is empty")
        try:
            decimal_value = Decimal(text)
        except InvalidOperation as exc:
            raise MoneyError(f"invalid credits value: {value!r}") from exc
    else:
        raise MoneyError(f"invalid credits value: {value!r}")

    if decimal_value != decimal_value.to_integral_value():
        raise MoneyError(
            f"credits value has a fractional component not supported "
            f"by this economy: {value!r}"
        )
    return int(decimal_value)


def format_credits(value: Credits, *, suffix: str = " cr") -> str:
    """Format a confirmed credits value for player-facing display.

    Raises :class:`MoneyError` for malformed input (callers should catch
    this and present a controlled error rather than letting a traceback
    surface, matching how the rest of the presentation layer works).
    """
    amount = parse_credits(value)
    return f"{amount:,}{suffix}"


def format_credits_or_dash(value, *, suffix: str = " cr") -> str:
    """Best-effort display helper: format if possible, else show a dash.

    Useful for optional/diagnostic display contexts where a malformed
    value should degrade gracefully instead of aborting the whole screen.
    """
    if value is None:
        return "-"
    try:
        return format_credits(value, suffix=suffix)
    except MoneyError:
        return str(value)
