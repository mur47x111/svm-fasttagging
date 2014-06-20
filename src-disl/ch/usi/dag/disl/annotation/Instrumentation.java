package ch.usi.dag.disl.annotation;

import java.lang.annotation.Documented;
import java.lang.annotation.ElementType;
import java.lang.annotation.Target;


/**
 * Marks a class as a DiSL instrumentation class.
 * <p>
 * This annotation can be only used with classes.
 */
@Documented
@Target (ElementType.TYPE)
public @interface Instrumentation {
  // TODO add tool that will look for Instrumentation annotation and create meta-data for jar
  // http://stackoverflow.com/questions/3644069/java-6-annotation-processing-configuration-with-ant
}
