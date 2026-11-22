"use strict";

async function showReleaseDetails() {
  const targets = {
    python: { details: "python-release", link: "python-download" },
    godot: { details: "godot-release", link: "godot-download" },
  };
  try {
    const response = await fetch("/downloads/release.json", { cache: "no-store" });
    if (!response.ok) throw new Error("Release manifest unavailable");
    const manifest = await response.json();
    for (const artifact of manifest.artifacts || []) {
      const target = targets[artifact.client];
      if (!target) continue;
      const element = document.getElementById(target.details);
      const link = document.getElementById(target.link);
      if (element) element.textContent = `${manifest.version} · ${artifact.platform} · ${manifest.release_date} · ${(artifact.bytes / 1024 / 1024).toFixed(2)} MB · SHA-256 ${artifact.sha256}`;
      if (link) link.href = `/downloads/${encodeURIComponent(artifact.file)}`;
    }
  } catch (_error) {
    for (const target of Object.values(targets)) {
      const element = document.getElementById(target.details);
      if (element) element.textContent = "Release details are temporarily unavailable.";
    }
  }
}

document.addEventListener("DOMContentLoaded", showReleaseDetails);
