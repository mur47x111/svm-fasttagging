package ch.usi.dag.dislserver;

import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

import ch.usi.dag.disl.DiSL;
import ch.usi.dag.disl.exception.DiSLException;
import ch.usi.dag.disl.exception.DiSLInMethodException;


public abstract class DiSLServer {

    public static final String PROP_DEBUG = "debug";
    private static final boolean debug = Boolean.getBoolean(PROP_DEBUG);

    private static final String PROP_PORT = "dislserver.port";
    private static final int DEFAULT_PORT = 11217;
    private static final int port = Integer.getInteger(PROP_PORT, DEFAULT_PORT);

    private static final String PROP_TIME_STAT = "dislserver.timestat";
    private static final boolean timeStat = Boolean.getBoolean(PROP_TIME_STAT);

    private static final String PROP_CONT = "dislserver.continuous";
    private static final boolean continuous = Boolean.getBoolean(PROP_CONT);

    private static final String PROP_BYPASS = "dislserver.disablebypass";
    private static final boolean bypass = ! Boolean.getBoolean(PROP_BYPASS);

    private static final AtomicInteger workerCount = new AtomicInteger();
    private static final AtomicLong instrumentationTime = new AtomicLong();

    private static DiSL disl;
    private static ServerSocket listenSocket;

    //

    public static void main (final String [] args) {
        try {
            // TODO LB: Configure bypass on a per-request basis.
            disl = new DiSL (bypass);

            if (debug) {
                System.out.println ("DiSL-Server: starting...");
            }

            listenSocket = new ServerSocket (port);
            listenSocket.setReuseAddress (true);

            if (debug) {
                System.out.printf (
                    "DiSL-Server: listening on %s:%d\n",
                    listenSocket.getInetAddress ().getHostAddress (),
                    listenSocket.getLocalPort ()
                );
            }

            while (true) {
                final Socket clientSocket = listenSocket.accept ();
                if (debug) {
                    System.out.printf (
                        "DiSL-Server: connection from %s:%d\n",
                        clientSocket.getInetAddress ().getHostAddress (),
                        clientSocket.getPort ()
                    );
                }

                final MessageChannel mc = new MessageChannel (clientSocket);
                workerCount.incrementAndGet ();
                new Worker (mc, disl).start ();
            }

        } catch (final IOException ioe) {
            // report the exception as a server error
            //
            // FIXME LB: Yuck! Creating a new exception just to print it...
            reportError (new DiSLServerException (ioe));

        } catch (final Throwable throwable) {
            reportError (throwable);
        }
    }


    static void reportError (final Throwable throwable) {
        if (throwable instanceof DiSLException) {
            System.err.print ("DiSL-Server: error");

            // report during which method it happened
            if (throwable instanceof DiSLInMethodException) {
                System.err.printf (
                    " (while instrumenting method \"%s\")",
                    throwable.getMessage ()
                );

                // report what actually happened
                __printException (throwable.getCause ());
            } else {
                __printException (throwable);
            }

        } else if (throwable instanceof DiSLServerException) {
            System.err.print ("DiSL-Server: error");
            __printException (throwable);

        } else {
            // some other exception
            System.err.print ("DiSL-Server: fatal error: ");
            throwable.printStackTrace ();
        }
    }


    private static void __printException (final Throwable throwable) {
        // report message if present
        final String message = throwable.getMessage ();
        System.err.println ((message != null) ? ": "+ message : "");

        // report inner errors
        Throwable cause = throwable.getCause ();
        while (cause != null && cause.getMessage () != null) {
            System.err.println ("  Inner error: " + cause.getMessage ());
            cause = cause.getCause ();
        }

        // dump stack trace in debug mode
        if (debug) {
            throwable.printStackTrace ();
        }
    }


    static void workerDone (final long instrTime) {
        instrumentationTime.addAndGet (instrTime);

        if (workerCount.decrementAndGet () == 0) {
            if (timeStat) {
                System.out.printf (
                    "DiSL-Server: instrumentation took %d milliseconds\n",
                    instrumentationTime.get () / 1000000
                );
            }

            // no workers - shutdown
            if (!continuous) {
                disl.terminate ();

                if (debug) {
                    System.out.println ("DiSL-Server: shutting down...");
                }

                System.exit(0);
            }
        }
    }

}
