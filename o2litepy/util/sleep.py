import time

implied_wakeup = 0  # Initialize at module level


def o2l_sleep(n):
    global implied_wakeup
    now = time.time() * 1000  # Convert current time to milliseconds
    if now - implied_wakeup < 50:
        implied_wakeup += n
    else:
        implied_wakeup = now + n

    if implied_wakeup > now + 1:
        sleep_time = (implied_wakeup - now) / 1000  # Convert back to seconds
        time.sleep(sleep_time)
