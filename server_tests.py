import socket
import json

# --- Test Data Configuration ---
# You can modify these dictionaries to test different scenarios.

# Valid data for a user that exists on the server
VALID_USER = {
    "player_num": 1,
    "player_name": "thor",
    "password": "hammer",
    "ship_num": 1,
    "sector": 1,
    "ship_name": "Valhalla",
    "ship_type": 1,
}

# Invalid data for a user that does not exist or has an incorrect password
INVALID_USER = {
    "player_num": 999,
    "player_name": "loki",
    "password": "lie",
    "ship_num": 999,
    "sector": 999,
    "ship_name": "Jotunheim",
    "ship_type": 1,
}

# Server connection details
HOST = 'localhost'
PORT = 1234
TIMEOUT = 5

# --- Utility Functions ---

def send_and_receive(sock, message_dict):
    """Sends a JSON message and returns the server's response dictionary."""
    try:
        # Serialize the dictionary to a JSON string and encode it to bytes
        json_message = json.dumps(message_dict) + '\n'
        sock.sendall(json_message.encode('utf-8'))

        # Receive the JSON response
        response_bytes = sock.recv(4096)
        response_string = response_bytes.decode('utf-8').strip()

        # Deserialize the JSON string back to a dictionary
        response_dict = json.loads(response_string)
        return response_dict

    except socket.timeout:
        return {"status": "TIMEOUT"}
    except json.JSONDecodeError:
        return {"status": "ERROR", "message": "Invalid JSON response from server"}
    except Exception as e:
        return {"status": "ERROR", "message": str(e)}

def run_test(test_name, sock, message_dict, expected_status=None):
    """
    Runs a single test case with a JSON message and prints the result.
    Args:
        test_name (str): The name of the test.
        sock (socket.socket): The socket to use.
        message_dict (dict): The message to send as a Python dictionary.
        expected_status (str, optional): The expected status field in the response.
    """
    print(f"TRIED: {test_name}")
    response = send_and_receive(sock, message_dict)
    print(f"RETURNED: {response}")

    test_status = "PASS"
    if expected_status and response.get("status") != expected_status:
        test_status = "FAIL"
    elif response.get("status") in ["TIMEOUT", "ERROR"]:
        test_status = "FAIL"

    print(f"TEST: {test_status}")
    print("-" * 20)
    return response

# --- Test Functions ---

def test_login(sock, user_data):
    """Tests the USER login command using JSON."""
    test_name = f"USER login for '{user_data['player_name']}'"
    command_dict = {
        "command": "USER",
        "player_name": user_data['player_name'],
        "password": user_data['password']
    }
    run_test(test_name, sock, command_dict, "OK")

def test_new_user(sock, user_data):
    """Tests the NEW user command using JSON."""
    test_name = f"NEW user creation for '{user_data['player_name']}'"
    command_dict = {
        "command": "NEW",
        "player_name": user_data['player_name'],
        "password": user_data['password'],
        "ship_name": user_data['ship_name']
    }
    run_test(test_name, sock, command_dict, "OK")

def test_description_command(sock):
    """Tests the DESCRIPTION command using JSON."""
    test_name = "DESCRIPTION"
    command_dict = {"command": "DESCRIPTION"}
    run_test(test_name, sock, command_dict, "OK")

def test_move_command(sock, sector_num):
    """Tests the MOVE command using JSON."""
    test_name = f"Move to sector {sector_num}"
    command_dict = {
        "command": "MOVE",
        "sector_num": sector_num
    }
    run_test(test_name, sock, command_dict, "OK")

def test_myinfo_command(sock):
    """Tests the MYINFO command using JSON."""
    test_name = "MYINFO"
    command_dict = {"command": "MYINFO"}
    run_test(test_name, sock, command_dict, "OK")

def test_playerinfo_command(sock, player_num):
    """Tests the PLAYERINFO command using JSON."""
    test_name = f"PLAYERINFO for player {player_num}"
    command_dict = {
        "command": "PLAYERINFO",
        "player_num": player_num
    }
    run_test(test_name, sock, command_dict, "OK")

def test_shipinfo_command(sock, ship_num):
    """Tests the SHIPINFO command using JSON."""
    test_name = f"SHIPINFO for ship {ship_num}"
    command_dict = {
        "command": "SHIPINFO",
        "ship_num": ship_num
    }
    run_test(test_name, sock, command_dict, "OK")

def test_quit_command(sock):
    """Tests the QUIT command using JSON."""
    test_name = "QUIT"
    command_dict = {"command": "QUIT"}
    run_test(test_name, sock, command_dict, "OK")

def test_online_command(sock):
    """Tests the ONLINE command using JSON."""
    test_name = "ONLINE"
    command_dict = {"command": "ONLINE"}
    run_test(test_name, sock, command_dict, "OK")

# --- Main Execution ---

if __name__ == "__main__":
    print("--- Running TWClone Protocol Tests ---")
    print("--- Testing with VALID User Data ---")

    try:
        # Create a socket and connect
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(TIMEOUT)
            s.connect((HOST, PORT))

            # Run test suite for a valid user
            print("\nAttempting to log in with valid credentials...")
            test_login(s, VALID_USER)

            print("\nRunning post-login commands:")
            test_description_command(s)
            test_myinfo_command(s)
            test_playerinfo_command(s, VALID_USER['player_num'])
            test_shipinfo_command(s, VALID_USER['ship_num'])
            test_online_command(s)

            # Test some invalid commands after login
            print("\nTesting known-bad commands after login:")
            run_test("Invalid command", s, {"command": "INVALID_COMMAND"}, "BAD_COMMAND")
            run_test("Malformed PLAYERINFO command", s, {"command": "PLAYERINFO"}, "BAD_COMMAND")
            
            test_quit_command(s)

    except ConnectionRefusedError:
        print(f"\nERROR: Could not connect to {HOST}:{PORT}. Make sure the server is running.")
    except Exception as e:
        print(f"\nAn unexpected error occurred: {e}")

    print("\n" + "=" * 40)
    print("--- Running TWClone Protocol Tests ---")
    print("--- Testing with INVALID User Data ---")

    try:
        # Create a new socket for the next test
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(TIMEOUT)
            s.connect((HOST, PORT))

            # Test invalid login attempt
            print("\nAttempting to log in with invalid credentials...")
            test_login(s, INVALID_USER)

            test_new_user(s, INVALID_USER)

            # Test commands without being logged in (should fail)
            print("\nRunning commands before logging in:")
            run_test("MYINFO (pre-login)", s, {"command": "MYINFO"}, "BAD_AUTH")
            run_test("DESCRIPTION (pre-login)", s, {"command": "DESCRIPTION"}, "BAD_AUTH")

            test_quit_command(s)

    except ConnectionRefusedError:
        print(f"\nERROR: Could not connect to {HOST}:{PORT}. Make sure the server is running.")
    except Exception as e:
        print(f"\nAn unexpected error occurred: {e}")
