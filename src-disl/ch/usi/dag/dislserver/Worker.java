package ch.usi.dag.dislserver;

import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicLong;

import ch.usi.dag.disl.DiSL;
import ch.usi.dag.disl.DiSL.CodeOption;
import ch.usi.dag.disl.exception.DiSLException;
import ch.usi.dag.disl.util.Constants;
import ch.usi.dag.util.Assert;
import ch.usi.dag.util.Strings;

final class Worker extends Thread {

    private static final boolean debug = Boolean .getBoolean(DiSLServer.PROP_DEBUG);

    private static final String PROP_UNINSTR = "dislserver.uninstrumented";
    private static final String uninstrPath = System.getProperty(PROP_UNINSTR, null);

    private static final String PROP_INSTR = "dislserver.instrumented";
    private static final String instrPath = System.getProperty(PROP_INSTR, null);

    // used for replies
    private static final byte [] emptyByteArray = new byte [0];

    //

    private final DiSL disl;

    private final MessageChannel messageChannel;

    private final AtomicLong instrumentationNanos = new AtomicLong ();

    //

    Worker (final MessageChannel messageChannel, final DiSL disl) {
        this.messageChannel = messageChannel;
        this.disl = disl;
    }

    @Override
    public void run() {
        try {
            //
            // Process requests until a shutdown request is received.
            //
            while (true) {
                final Message request = messageChannel.recvMessage ();
                if (request.isShutdown ()) {
                    return;
                }

                __processRequest (request);
            }

        } catch (final IOException ioe) {
            // report IO exception as a server error
            DiSLServer.reportError (new DiSLServerException (ioe));
        } catch (final Throwable e) {
            DiSLServer.reportError (e);
        } finally {
            messageChannel.close ();
            DiSLServer.workerDone (instrumentationNanos.get ());
        }
    }

    private void __processRequest (final Message request) throws Exception {
        // get data from request
        final byte [] classBytes = request.payload ();
        final String className = __getClassName (request.control (), classBytes);
        final Set <CodeOption> options = CodeOption.setOf (request.flags ());
        __showRequestIfDebug (className, classBytes, options);

        try {
            // instrument the class and time it
            final long startTime = System.nanoTime ();
            final byte [] newClassBytes = __instrumentClass (className, classBytes, options);
            instrumentationNanos.addAndGet (System.nanoTime () - startTime);

            // send response
            final Message response = (newClassBytes != null)
                ? Message.createClassModifiedResponse (newClassBytes)
                : Message.createNoOperationResponse ();

            messageChannel.sendMessage (response);

        } catch (final Exception e) {
            //
            // Report any exception as server-side error to the client.
            // In such case, the control part of the message contains the
            // error message and the payload is empty.
            //
            final String message = __getErrorMessage (e);

            // error protocol:
            // control contains the description of the server-side error
            // class code is an array of size zero
            final String errMsg = "Instrumentation error for class "
                    + className + ": " + message;

            messageChannel.sendMessage (
                new Message (-1, errMsg.getBytes (), emptyByteArray)
            );

            throw e;
        }
    }


    private String __getErrorMessage (final Exception e) {
        if (!debug) {
            return e.getMessage ();

        } else {
            // during debug send the whole message
            final StringWriter result = new StringWriter ();
            e.printStackTrace (new PrintWriter (result));
            return result.toString ();
        }
    }


    private void __showRequestIfDebug (
        final String className, final byte [] classCode,
        final Set <CodeOption> options
    ) {
        if (debug) {
            System.out.printf (
                "DiSL-Server: instrumenting class %s [%d bytes",
                className.isEmpty () ? "<unknown>" : className,
                classCode.length
            );

            for (final CodeOption option : options) {
                System.out.printf (", %s", option);
            }

            System.out.println ("]");
        }
    }


    private static String __getClassName (final byte [] nameBytes, final byte [] codeBytes) {
        String result = Strings.EMPTY_STRING;
        if (nameBytes.length > 0) {
            result = new String (nameBytes);
        }

        if (result.isEmpty ()) {
            result = __parseInternalClassName (codeBytes);
            if (result == null || result.isEmpty ()) {
                result = UUID.randomUUID ().toString ();
            }
        }

        return result;
    }


    private static String __parseInternalClassName (final byte [] byteCode) {
        final int CLASS_MAGIC = 0xCAFEBABE;

        final int TAG_CONSTANT_UTF8 = 1;
        final int TAG_CONSTANT_LONG = 5;
        final int TAG_CONSTANT_DOUBLE = 6;
        final int TAG_CONSTANT_CLASS = 7;
        final int TAG_CONSTANT_STRING = 8;
        final int TAG_CONSTANT_METHOD_HANDLE = 15;
        final int TAG_CONSTANT_METHOD_TYPE = 16;

        //

        try (
            final DataInputStream dis = new DataInputStream (
                new ByteArrayInputStream (byteCode)
            );
        ) {
            // verify magic field
            if (dis.readInt () != CLASS_MAGIC) {
                throw new IOException ("invalid class file format");
            }

            // skip minor_version and major_version fields
            dis.skipBytes (2);
            dis.skipBytes (2);

            //
            // Scan the constant pool to pick up the UTF-8 strings and the
            // class info references to those strings. Skip everything else.
            // Valid index into the constant pool must be greater than 0.
            //
            final int constantCount = dis.readUnsignedShort ();
            final int [] classIndices = new int [constantCount];
            final String [] utfStrings = new String [constantCount];

            for (int poolIndex = 1; poolIndex < constantCount; poolIndex++) {
                final int poolTag = dis.readUnsignedByte ();

                switch (poolTag) {
                case TAG_CONSTANT_UTF8:
                    utfStrings [poolIndex] = dis.readUTF ();
                    break;

                case TAG_CONSTANT_CLASS:
                    classIndices [poolIndex] = dis.readUnsignedShort ();
                    break;

                case TAG_CONSTANT_STRING:
                case TAG_CONSTANT_METHOD_TYPE:
                    dis.skipBytes (2);
                    break;

                case TAG_CONSTANT_METHOD_HANDLE:
                    dis.skipBytes (3);
                    break;

                case TAG_CONSTANT_LONG:
                case TAG_CONSTANT_DOUBLE:
                    dis.skipBytes (8);

                    // 64-bit values take up two constant pool slots
                    poolIndex++;
                    break;

                default:
                    // all other constant structures fit into 4 bytes
                    dis.skipBytes (4);
                }
            }

            // skip access_flags field
            dis.skipBytes (2);

            // get this_class constant pool index
            final int thisClassIndex = dis.readUnsignedShort ();

            // resolve the (internal) class name
            return utfStrings [classIndices [thisClassIndex]];

        } catch (final IOException ioe) {
            // failed to parse class name
            return null;
        }
    }

    private byte [] __instrumentClass (
        final String className, final byte [] origCode, final Set <CodeOption> options
    ) throws DiSLServerException, DiSLException {
        Assert.stringNotEmpty (className, "className");
        Assert.objectNotNull (origCode, "origCode");
        Assert.objectNotNull (options, "options");
        
        //

        // dump uninstrumented byte code
        if (uninstrPath != null) {
            __dumpClass (className, origCode, uninstrPath);
        }

        // TODO: instrument the bytecode according to given options
        // byte [] instrCode = disl.instrument (origCode, options);
        final byte [] instrCode = disl.instrument (origCode);

        // dump instrumented byte code
        if (instrPath != null && instrCode != null) {
            __dumpClass (className, instrCode, instrPath);
        }

        return instrCode;
    }

    private static void __dumpClass (
        final String className, final byte[] byteCode, final String path
    ) throws DiSLServerException {
        // extract the class name and package name
        final int i = className.lastIndexOf (Constants.PACKAGE_INTERN_DELIM);
        final String simpleClassName = className.substring (i + 1);
        final String packageName = className.substring (0, i + 1);

        // construct path to the class
        final String pathWithPkg = path + File.separator + packageName;

        // create directories
        new File (pathWithPkg).mkdirs ();

        // dump the class code
        try (
            final FileOutputStream fo = new FileOutputStream (
                pathWithPkg + simpleClassName + Constants.CLASS_EXT
            );
        ) {
            fo.write(byteCode);
        } catch (final IOException ioe) {
            throw new DiSLServerException (ioe);
        }
    }

    //

    @SuppressWarnings ("unused")
    private void __debug (final String format, final Object ... args) {
        if (debug) {
            System.out.printf (format, args);
        }
    }

}
