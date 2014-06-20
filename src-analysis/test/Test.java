package test;

public class Test {

    public static void foo (final Object object) {

    }


    public static void main (final String [] args) {
        for (int i = 0; i < 100; i++) {
            foo (new Object ());
        }
    }
}
