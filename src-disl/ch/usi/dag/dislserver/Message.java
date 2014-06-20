package ch.usi.dag.dislserver;



final class Message {

    private static final byte [] __EMPTY_ARRAY__ = new byte [0];

    //

    private final int __flags;

    private final byte [] __control;

    private final byte [] __payload;

    //

    public Message (final int flags) {
        this (flags, __EMPTY_ARRAY__, __EMPTY_ARRAY__);
    }

    public Message (final int flags, final byte [] control) {
        this (flags, control, __EMPTY_ARRAY__);
    }

    public Message (
        final int flags, final byte [] control, final byte [] payload
    ) {
        __flags = flags;
        __control = control;
        __payload = payload;
    }

    //

    public int flags () {
        return __flags;
    }


    public byte [] control () {
        return __control;
    }


    public byte [] payload () {
        return __payload;
    }

    //

    public boolean isShutdown () {
        return (__control.length == 0) && (__payload.length == 0);
    }

    //

    public static Message createClassModifiedResponse (final byte [] instrClass) {
        return new Message (0, __EMPTY_ARRAY__, instrClass);
    }

    public static Message createNoOperationResponse () {
        return new Message (0, __EMPTY_ARRAY__, __EMPTY_ARRAY__);
    }

}
