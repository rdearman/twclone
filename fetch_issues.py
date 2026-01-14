import subprocess
import json

def fetch_issues():
    print("Fetching issues for milestone v2.0...")
    try:
        # Fetch json with number, title, and body
        result = subprocess.run(
            ["gh", "issue", "list", "--repo", "rdearman/twclone", "--milestone", "v2.0", "--limit", "100", "--json", "number,title,body"],
            capture_output=True,
            text=True,
            check=True
        )
        issues = json.loads(result.stdout)
        
        with open("issues_v2.0.json", "w") as f:
            json.dump(issues, f, indent=2)
            
        print(f"Successfully saved {len(issues)} issues to issues_v2.0.json")
        
    except subprocess.CalledProcessError as e:
        print(f"Error running gh: {e.stderr}")
    except Exception as e:
        print(f"An error occurred: {e}")

if __name__ == "__main__":
    fetch_issues()