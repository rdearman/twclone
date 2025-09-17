import socket

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

def send_and_receive(sock, message):
    """Sends a message and returns the server's response."""
    try:
        sock.sendall(f"{message}\n".encode())
        response = sock.recv(4096).decode('utf-8').strip()
        return response
    except socket.timeout:
        return "TIMEOUT"
    except Exception as e:
        return f"ERROR: {e}"

def run_test(test_name, sock, message, expected_starts_with=None):
    """
    Runs a single test case and prints the result.
    Args:
        test_name (str): The name of the test.
        sock (socket.socket): The socket to use.
        message (str): The message to send to the server.
        expected_starts_with (str, optional): The expected start of the response.
                                              If None, any response is considered successful.
    """
    print(f"TRIED: {test_name}")
    response = send_and_receive(sock, message)
    print(f"RETURNED: {response}")

    test_status = "PASS"
    if expected_starts_with and not response.startswith(expected_starts_with):
        test_status = "FAIL"
    elif not expected_starts_with and response == "BAD":
        test_status = "FAIL"
    elif response == "TIMEOUT" or "ERROR" in response:
        test_status = "FAIL"

    print(f"TEST: {test_status}")
    print("-" * 20)
    return response

# --- Test Functions ---

def test_login(sock, user_data):
    """Tests the USER login command."""
    test_name = f"USER login for '{user_data['player_name']}'"
    command = f"USER {user_data['player_name']}:{user_data['password']}:"
    run_test(test_name, sock, command, ":")

def test_new_user(sock, user_data):
    """Tests the NEW user command."""
    test_name = f"NEW user creation for '{user_data['player_name']}'"
    command = f"NEW {user_data['player_name']}:{user_data['password']}:{user_data['ship_name']}:"
    run_test(test_name, sock, command, "OK")

def test_description_command(sock):
    """Tests the DESCRIPTION command."""
    test_name = "DESCRIPTION"
    run_test(test_name, sock, "DESCRIPTION", ":")

def test_move_command(sock, sector_num):
    """Tests the move command <#>."""
    test_name = f"Move to sector {sector_num}"
    run_test(test_name, sock, str(sector_num), ":")

def test_myinfo_command(sock):
    """Tests the MYINFO command."""
    test_name = "MYINFO"
    run_test(test_name, sock, "MYINFO", ":")

def test_playerinfo_command(sock, player_num):
    """Tests the PLAYERINFO command."""
    test_name = f"PLAYERINFO for player {player_num}"
    command = f"PLAYERINFO {player_num}:"
    run_test(test_name, sock, command, ":")

def test_shipinfo_command(sock, ship_num):
    """Tests the SHIPINFO command."""
    test_name = f"SHIPINFO for ship {ship_num}"
    command = f"SHIPINFO {ship_num}:"
    run_test(test_name, sock, command, ":")

def test_quit_command(sock):
    """Tests the QUIT command."""
    test_name = "QUIT"
    run_test(test_name, sock, "QUIT", "OK")

def test_online_command(sock):
    """Tests the ONLINE command."""
    test_name = "ONLINE"
    run_test(test_name, sock, "ONLINE", ":")

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
            run_test("Invalid command", s, "INVALID_COMMAND", "BAD")
            run_test("Malformed PORT command", s, "PORT:", "BAD")
            run_test("Malformed PLAYERINFO command", s, "PLAYERINFO:", "BAD")

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
            run_test("MYINFO (pre-login)", s, "MYINFO", "BAD")
            run_test("DESCRIPTION (pre-login)", s, "DESCRIPTION", "BAD")
            
            test_quit_command(s)
            
    except ConnectionRefusedError:
        print(f"\nERROR: Could not connect to {HOST}:{PORT}. Make sure the server is running.")
    except Exception as e:
        print(f"\nAn unexpected error occurred: {e}")
