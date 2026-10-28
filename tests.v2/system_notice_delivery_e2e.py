#!/usr/bin/env python3
"""Live and reconnect delivery regression for durable system notices."""
import os
import socket
import sys
import time
import uuid

from twclient import TWClient

HOST = os.getenv("HOST", "127.0.0.1")
PORT = int(os.getenv("PORT", "1234"))


def wait_response(client, request_id, timeout=10):
    deadline = time.time() + timeout
    while time.time() < deadline:
        message = client.recv_json()
        if not message:
            raise ConnectionError("server closed connection before RPC response")
        if message.get("reply_to") == request_id:
            return message
    raise TimeoutError("RPC response timed out")


def command(client, name, data=None):
    request = {"command": name}
    if data is not None:
        request["data"] = data
    client.send_json(request)
    return wait_response(client, request["id"])


def matching_notice(client, body, timeout=10):
    deadline = time.time() + timeout
    while time.time() < deadline:
        message = client.recv_json()
        if message.get("type") == "system.notice_v1" and message.get("data", {}).get("body") == body:
            return message
    raise TimeoutError("expected system.notice_v1 was not delivered")


def notice_absent(client, body, timeout=0.5):
    old_timeout = client.sock.gettimeout()
    deadline = time.time() + timeout
    try:
        while time.time() < deadline:
            client.sock.settimeout(max(0.01, deadline - time.time()))
            try:
                message = client.recv_json()
            except socket.timeout:
                return True
            if message.get("type") == "system.notice_v1" and message.get("data", {}).get("body") == body:
                return False
        return True
    finally:
        client.sock.settimeout(old_timeout)


def main():
    suffix = uuid.uuid4().hex[:12]
    username = f"notice_e2e_{suffix}"
    password = "notice-e2e-password"
    live_body = f"live-notice-{suffix}"
    reconnect_body = f"reconnect-notice-{suffix}"
    observer = TWClient(HOST, PORT, timeout=20)
    admin = TWClient(HOST, PORT, timeout=20)
    reconnect = None
    final_reconnect = None
    try:
        observer.connect()
        if not observer.register(username, password, fail_if_exists=True):
            raise AssertionError("observer account registration failed")
        if not observer.login(username, password):
            raise AssertionError("observer login failed")

        subscriptions = command(observer, "subscribe.list")
        topics = subscriptions.get("data", {}).get("topics", [])
        if (subscriptions.get("status") != "ok"
                or not all(isinstance(topic, str) for topic in topics)
                or "system.notice" not in topics):
            raise AssertionError("authenticated account lacks enabled system.notice subscription")

        admin.connect()
        if not admin.login("System", "BOT"):
            raise AssertionError("sysop login failed")
        created = command(admin, "sys.notice.create", {
            "title": f"Notice delivery live {suffix}", "body": live_body, "severity": "info"
        })
        if created.get("status") != "ok":
            raise AssertionError(f"system notice creation failed: {created}")
        live_event = matching_notice(observer, live_body)
        if live_event.get("meta", {}).get("topic") != "system.notice":
            raise AssertionError("live notice topic metadata changed")
        if not live_event.get("meta", {}).get("mandatory") or not live_event.get("meta", {}).get("persistent"):
            raise AssertionError("live notice mandatory/persistent metadata missing")
        if live_event.get("data", {}).get("id", 0) <= 0:
            raise AssertionError("live notice has no persisted notice id")
        if not notice_absent(observer, live_body, timeout=0.35):
            raise AssertionError("live notice was delivered more than once")
        ack = command(observer, "notice.ack", {"notice_id": live_event["data"]["id"]})
        if ack.get("status") != "ok":
            raise AssertionError("live notice acknowledgement failed")

        observer.close()
        created = command(admin, "sys.notice.create", {
            "title": f"Notice delivery reconnect {suffix}", "body": reconnect_body, "severity": "warn"
        })
        if created.get("status") != "ok":
            raise AssertionError(f"offline system notice creation failed: {created}")

        reconnect = TWClient(HOST, PORT, timeout=20)
        reconnect.connect()
        if not reconnect.login(username, password):
            raise AssertionError("observer reconnect login failed")
        resumed_event = matching_notice(reconnect, reconnect_body)
        if resumed_event.get("meta", {}).get("persistent") is not True:
            raise AssertionError("reconnect notice was not marked persistent")
        ack = command(reconnect, "notice.ack", {"notice_id": resumed_event["data"]["id"]})
        if ack.get("status") != "ok":
            raise AssertionError("reconnect notice acknowledgement failed")

        reconnect.close()
        final_reconnect = TWClient(HOST, PORT, timeout=20)
        final_reconnect.connect()
        if not final_reconnect.login(username, password):
            raise AssertionError("second reconnect login failed")
        if not notice_absent(final_reconnect, reconnect_body):
            raise AssertionError("acknowledged notice was replayed after reconnect")

        print("PASS: system.notice live delivery, persistence, acknowledgement and reconnect replay")
        return 0
    finally:
        observer.close()
        admin.close()
        if reconnect:
            reconnect.close()
        if final_reconnect:
            final_reconnect.close()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
