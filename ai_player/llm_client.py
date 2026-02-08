import httpx
import json
import logging
import re
import random
import time

logger = logging.getLogger(__name__)

# --- Configuration ---
# You might want to move these to your config.json eventually
OLLAMA_API_URL = "http://localhost:11434/api/generate"
#DEFAULT_MODEL = "phi3:latest"
DEFAULT_MODEL = "dolphin-max:latest"
REQUEST_TIMEOUT = 300  # 5 minutes - account for Ollama queueing with concurrent bots

def parse_llm_json(response_text):
    """
    Extracts a JSON object or list from a verbose LLM response.
    """
    if not response_text:
        return None

    try:
        # 1. Try strict parsing first (fastest)
        return json.loads(response_text)
    except json.JSONDecodeError:
        pass

    # 2. Pattern matching: Find the first '{' or '[' and the last '}' or ']'
    # This ignores "Here is your plan:" at the start and explanations at the end.
    try:
        # Search for array or object
        match = re.search(r'(\{|\[).*(\}|\])', response_text, re.DOTALL)
        if match:
            json_str = match.group(0)
            return json.loads(json_str)
    except Exception as e:
        pass
        
    # 3. Last resort: specific cleanup for common markdown issues
    try:
        clean_text = response_text.replace("```json", "").replace("```", "").strip()
        return json.loads(clean_text)
    except Exception:
        return None

def get_ollama_response(game_state, model=None, prompt_key=None, override_prompt=None):
    """
    Sends a prompt and game state to the Ollama server and gets a response.
    
    :param game_state: The full state dictionary.
    :param model: The model to use (e.g., "llama3:latest").
    :param prompt_key: The key for a pre-defined prompt (not used here, but good practice).
    :param override_prompt: The full prompt string to send.
    :return: The JSON response string from the model, or None on failure.
    """
    
    if not model:
        model = DEFAULT_MODEL
    
    # Use the override_prompt if provided, otherwise build one (though we expect one)
    if override_prompt:
        # Format the prompt with the game state
        # We must serialize the game_state to a compact JSON string
        try:
            state_str = json.dumps(
                game_state,
                separators=(',', ':'),
                default=lambda o: f"<<nonserializable:{type(o).__name__}>>"
            )
            full_prompt = override_prompt.replace("{game_state}", state_str)
        except Exception as e:
            logger.error(f"Failed to serialize game state for LLM: {e}")
            return None
    else:
        logger.error("get_ollama_response called without a prompt.")
        return None

    # --- Build the request payload for Ollama ---
    payload = {
        "model": model,
        "prompt": full_prompt,
        "stream": False,  # We want the full response at once
        "format": "json",  # We asked the LLM to respond in JSON
        "options": {
            "num_predict": 128  # Limit response length to save CPU
        }
    }

    logger.debug(f"Sending prompt to Ollama model {model}...")

    # --- Make the API call ---
    try:
        with httpx.Client(timeout=REQUEST_TIMEOUT) as client:
            response = client.post(OLLAMA_API_URL, json=payload)
            response.raise_for_status()  # Raise an exception for bad status codes

        response_data = response.json()
        
        # The actual content is in the 'response' key
        model_response_str = response_data.get("response")
        
        if model_response_str:
            logger.debug(f"Ollama raw response: {model_response_str}")
            return model_response_str
        else:
            logger.error("Ollama response received, but 'response' key was empty.")
            return None

    except httpx.TimeoutException as e:
        logger.error(f"Timeout calling Ollama after {REQUEST_TIMEOUT}s: {e}")
        return None
    except httpx.HTTPStatusError as e:
        logger.error(f"HTTP error calling Ollama: {e.response.status_code} - {e.response.text}")
        return None
    except httpx.RequestError as e:
        logger.error(f"Error connecting to Ollama at {OLLAMA_API_URL}. Is it running? Error: {e}")
        return None
    except json.JSONDecodeError as e:
        logger.error(f"Failed to decode Ollama's response as JSON: {e}")
        return None
    except Exception as e:
        logger.error(f"Unexpected error in get_ollama_response: {e}", exc_info=True)
        return None

# --- Main entry point for testing ---
if __name__ == "__main__":
    # This allows you to test the file directly: python llm_client.py
    print("Testing Ollama client...")
    
    # A minimal dummy state for testing
    dummy_state = {
        "player_location_sector": 1,
        "ship_info": {"cargo": {"organics": 20}, "holds": 25},
        "strategy_plan": []
    }
    
    # A dummy prompt (same as in main.py)
    dummy_prompt = """
    You are a master space trader. Your goal is to make profit.
    Based on the current game state, provide a high-level strategic plan.
    Return your plan as a JSON list of simple goal strings.
    Example response:
    ["goto: 127", "sell: ORG", "buy: ORE", "goto: 271", "sell: ORE"]
    Current state:
    {game_state}
    What is your plan?
    """
    
    response = get_ollama_response(
        dummy_state, 
        DEFAULT_MODEL, 
        "PROMPT_STRATEGY", 
        override_prompt=dummy_prompt
    )
    
    if response:
        print("\n--- Test Response from Ollama ---")
        print(response)
        try:
            # Try to parse the model's response string as JSON
            plan = json.loads(response)
            print("\n--- Parsed as JSON ---")
            print(plan)
        except:
            print("\n--- Model response was not valid JSON ---")
    else:
        print("\nFailed to get a response from Ollama.")
        print(f"Make sure Ollama is running and serving '{DEFAULT_MODEL}' at {OLLAMA_API_URL}")
