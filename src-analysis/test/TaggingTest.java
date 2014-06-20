package test;

import java.util.LinkedList;


public class TaggingTest extends Thread {

    private final LinkedList <Object> list = new LinkedList <> ();


    public void add (final Object object) {
        list.add (object);
    }


    public void remove (final Object object) {
        // list.remove (object);
    }


    public Object newInstance () {
        return new Object ();
    }


    // public static Unsafe unsafe;
    //
    // static {
    // try {
    // final Field field = Unsafe.class.getDeclaredField ("theUnsafe");
    // field.setAccessible (true);
    // unsafe = (Unsafe) field.get (null);
    // } catch (final Exception e) {
    // unsafe = null;
    // }
    // }
    //
    // static boolean test = true;
    //
    //
    // public void validate (final Object object) {
    // // final long klass = unsafe.getLong (object, 8L);
    // final long tag = unsafe.getLong (object, 16L);
    //
    // if (tag != 0) {
    // System.err.printf ("Initial tag is %016X\n", tag);
    // // test = false;
    // }
    // }

    @Override
    public void run () {
        for (int i = 0; i < 100000; i++) {
            final Object obj = newInstance ();
            add (obj);
        }

        for (final Object object : list) {
            remove (object);
        }

        // System.out.println (list.size ());
    }


    public static void main (final String [] args) {
        for (int i = 0; i < 32; i++) {
            new TaggingTest ().run ();
        }

        // try {
        // sleep (10000);
        // } catch (final InterruptedException e) {}
    }

}
