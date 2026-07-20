(() => {
  "use strict";

  const clock = document.getElementById("clock");
  const spokenTime = document.getElementById("spoken-time");
  const parameters = new URLSearchParams(window.location.search);
  let showSeconds = parameters.get("seconds") === "1";
  let timer = 0;

  function updateClock() {
    const now = new Date();
    const seconds = now.getSeconds() + now.getMilliseconds() / 1000;
    const minutes = now.getMinutes() + seconds / 60;
    const hours = (now.getHours() % 12) + minutes / 60;

    document.documentElement.style.setProperty(
      "--hour-angle",
      `${hours * 30}deg`
    );
    document.documentElement.style.setProperty(
      "--minute-angle",
      `${minutes * 6}deg`
    );
    document.documentElement.style.setProperty(
      "--second-angle",
      `${seconds * 6}deg`
    );
    spokenTime.textContent = now.toLocaleTimeString();
  }

  function scheduleUpdate() {
    window.clearTimeout(timer);
    updateClock();

    const now = new Date();
    const interval = showSeconds ? 1000 : 60000;
    const elapsed = showSeconds
      ? now.getMilliseconds()
      : now.getSeconds() * 1000 + now.getMilliseconds();
    timer = window.setTimeout(scheduleUpdate, interval - elapsed + 8);
  }

  function applySecondsPreference() {
    clock.classList.toggle("show-seconds", showSeconds);
    clock.setAttribute("aria-pressed", showSeconds ? "true" : "false");
    scheduleUpdate();
  }

  clock.addEventListener("click", () => {
    showSeconds = !showSeconds;
    applySecondsPreference();
  });

  document.addEventListener("visibilitychange", () => {
    if (document.hidden) {
      window.clearTimeout(timer);
    } else {
      scheduleUpdate();
    }
  });

  applySecondsPreference();
})();
