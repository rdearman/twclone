import uuid
import logging

logger = logging.getLogger(__name__)

def canon_commodity(s):
    """
    Normalizes a commodity string to the canonical 3-letter uppercase code.
    Returns 'ORE', 'ORG', 'EQU', 'SLV', 'WPN', 'DRG', or 'COL'. 
    Returns None if input is invalid or unknown.
    """
    if not s:
        return None
    
    s = str(s).strip().upper()
    
    # Direct matches
    if s in ("ORE", "ORG", "EQU", "SLV", "WPN", "DRG", "COL"):
        return s
    
    # Common mappings
    if s.startswith("ORE"):
        return "ORE"
    if s.startswith("ORG") or "ORGANIC" in s:
        return "ORG"
    if s.startswith("EQU") or "EQUIP" in s:
        return "EQU"
    if s.startswith("SLV") or "SLAVE" in s:
        return "SLV"
    if s.startswith("WPN") or "WEAPON" in s:
        return "WPN"
    if s.startswith("DRG") or "DRUG" in s:
        return "DRG"
    if s.startswith("COL") or "COLONIST" in s:
        return "COL"
        
    # Fallback for "Fuel" or others if they become relevant, but for now strict protocol
    return None

def generate_idempotency_key():
    """Generates a UUID4 string for idempotency."""
    return str(uuid.uuid4())
