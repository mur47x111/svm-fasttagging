package ch.usi.dag.dislserver;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.net.Socket;


final class MessageChannel implements AutoCloseable {

    private final DataInputStream __is;
    private final DataOutputStream __os;

    //

    public MessageChannel (final Socket socket) throws IOException {
        socket.setTcpNoDelay (true);

        __is = new DataInputStream (
            new BufferedInputStream (socket.getInputStream ())
        );

        __os = new DataOutputStream (
            new BufferedOutputStream (socket.getOutputStream ())
        );
    }

    //

    public Message recvMessage () throws IOException {
        // request protocol:
        //
        // java int - request flags
        // java int - control data length (cdl)
        // java int - payload data length (pdl)
        // bytes[cdl] - control data (contains class name)
        // bytes[pdl] - payload data (contains class code)

        final int flags = __is.readInt ();
        final int controlLength = __is.readInt ();
        final int payloadLength = __is.readInt ();

        final byte [] control = new byte [controlLength];
        __is.readFully (control);

        final byte [] payload = new byte [payloadLength];
        __is.readFully (payload);

        return new Message (flags, control, payload);
    }


    public void sendMessage (Message message) throws IOException {
        // response protocol:
        //
        // java int - response flags
        // java int - control data length (cdl)
        // java int - payload data length (pdl)
        // bytes[cdl] - control data (nothing, or error message)
        // bytes[pdl] - payload data (instrumented class code)

        __os.writeInt (message.flags ());
        __os.writeInt (message.control ().length);
        __os.writeInt (message.payload ().length);

        __os.write (message.control ());
        __os.write (message.payload ());

        __os.flush();
    }


    @Override
    public void close () {
        try {
            __is.close ();
            __os.close ();
        } catch (final IOException e) {
            // not much we can do about it
        }
    }

}
