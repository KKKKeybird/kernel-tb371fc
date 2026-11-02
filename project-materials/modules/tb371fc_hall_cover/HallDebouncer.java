/** Edge-stability filter; timestamps are monotonic milliseconds. */
public final class HallDebouncer {
  public static final long DELAY_MS = 350;
  private Integer stable, candidate;
  private long due = -1;
  public boolean event(int value, long now) {
    if (value != 0 && value != 1) return false;
    if (stable == null) { stable = candidate = value; return false; }
    if (candidate == value) return false;
    candidate = value;
    due = stable == value ? -1 : now + DELAY_MS;
    return true;
  }
  public long due() { return due; }
  public Integer commit(long now) {
    if (due < 0 || now < due) return null;
    stable = candidate;
    due = -1;
    return stable;
  }
}
