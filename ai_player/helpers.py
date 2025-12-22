import uuid
import logging

logger = logging.getLogger(__name__)

def canon_commodity(s):
    """
    Normalizes a commodity string to the canonical 3-letter uppercase code.
    Returns 'ORE', 'ORG', or 'EQU'. Returns None if input is invalid or unknown.
    """
    if not s:
        return None
    
    s = str(s).strip().upper()
    
    # Direct matches
    if s in ("ORE", "ORG", "EQU"):
        return s
    
    # Common mappings
    if s.startswith("ORE"):
        return "ORE"
    if s.startswith("ORG") or "ORGANIC" in s:
        return "ORG"
    if s.startswith("EQU") or "EQUIP" in s:
        return "EQU"
        
    # Fallback for "Fuel" or others if they become relevant, but for now strict protocol
    return None

def generate_idempotency_key():
    """Generates a UUID4 string for idempotency."""
    return str(uuid.uuid4())
